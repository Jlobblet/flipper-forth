: [CHAR] CHAR LIT-COMPILE ; IMMEDIATE
: (
  1 BEGIN
    KEY DUP [CHAR] ( =
    IF DROP 1+
    ELSE [CHAR] ) = IF 1- THEN THEN
  DUP 0= UNTIL
  DROP
  ; IMMEDIATE

(
  Now we have multi-line comments!
  This algorithm pushes 1 to the stack (the opening bracket) and then consumes
  characters from the input buffer via KEY repeatedly. Each left bracket
  increments the counter by 1 and each right bracket decrements by 1. When the
  counter hits zero, the loop breaks, allowing for nested brackets (like in this
  comment!)
)

: \ 0XA PARSE 2DROP ; IMMEDIATE

\ Now we have line comments too!

\ Words for entering compile mode for immediate execution
: [ 0 STATE ! ; IMMEDIATE
: ] 1 STATE ! ;
: [: :NONAME ; IMMEDIATE
: ;] POSTPONE EXIT POSTPONE [ EXECUTE ; IMMEDIATE

\ Cheap word for creating aliases
: ALIAS ( "newname" "oldname" -- ) CREATE ' , DOES> @ EXECUTE ;
ALIAS NB. \

: ? ( addr -- ) @ . ;
: C? ( addr -- ) C@ . ;
: CELLS ( n -- n_c ) CELL * ;

: VARIABLE! CREATE , ;
: VARIABLE CREATE 0 , ;
: CONSTANT CREATE , DOES> @ ;
: ARRAY CREATE CELLS ALLOT DOES> SWAP CELLS + ;
: CARRAY CREATE ALLOT DOES> + ;

\ Words for manipulating array and carray pointers
: PTR++ ( a -- a++ ) DUP @ DUP CELL + ROT ! ;
: --PTR ( a -- --a ) DUP @ CELL - DUP ROT ! ;
: CPTR++ ( a -- a++ ) DUP @ DUP 1+ ROT ! ;
: --CPTR ( a -- --a ) DUP @ 1- DUP ROT ! ;

\ Print a string immediately
: ." POSTPONE S" STATE @ IF POSTPONE TYPE ELSE TYPE THEN ; IMMEDIATE

\ Some string printing utilities now
0X20 CONSTANT BL ;
: SPACE BL EMIT ;
: CR 0XA EMIT ;
\ Print n spaces
: SPACES ( n -- ) BEGIN DUP WHILE SPACE 1- REPEAT DROP ;

: ABS ( n -- |n| ) DUP 0< IF NEGATE THEN ;

\ Constants for Boolean flags
0 CONSTANT F
-1 CONSTANT T

: SQUARE ( n -- n^2 ) DUP * ;
: QUAD ( n -- n^4 ) SQUARE SQUARE ;

: FAC ( n -- n! )
  1 SWAP
  BEGIN DUP
  WHILE TUCK * SWAP 1-
  REPEAT DROP ;

: FIB ( n -- F_n )
  1 0 ROT
  BEGIN DUP
  WHILE -ROT SWAP TUCK + SWAP ROT 1-
  REPEAT DROP NIP ;

\ Store a counted string in the dictionary
: STRING, ( addr u -- a )
  HERE -ROT            ( a addr u )
  BEGIN DUP WHILE
    OVER C@ C,
    1- SWAP 1+ SWAP
  REPEAT 2DROP ;

\ Counted strings: store a length byte then the string after
: CSTRING, ( addr u -- c-addr ) HERE -ROT DUP C, STRING, DROP ;

\ Look up a counted string in regular string format
: COUNT ( c-addr -- addr u )  DUP 1+ SWAP C@ ;

\ Type a counted string
: CTYPE ( c-addr -- ) COUNT TYPE ;

\ String equality
: S= ( a1 u1 a2 u2 -- ? )
  ROT OVER <> IF 3DROP F EXIT THEN  \ test lengths
  ( a1 a2 u )
  BEGIN DUP WHILE
    -ROT 2DUP C@ SWAP C@ <> IF 3DROP F EXIT THEN
    1+ SWAP 1+ ROT 1-
  REPEAT 3DROP T ;

\ EXPR implementation
\ -------------------

(
  EXPR is a "bracketed" opt-in infix operator expression.

  The idea is that within the bounds of EXPR and ;EXPR, arithmetic will be
  evaluated using infix precedence levels. Words will push themselves to a
  level stack if their level is greater than or equal to than what is already
  on the stack. If their level is lower, they will evaluate what is there
  instead.

  EXPR can work in both modes.

  In interpret mode, we just need to either push operators to the level stack
  or evaluate them to the data stack as we go.

  In compile mode, the act of "evaluating" is actually compiling with , or F,.
  This is quite nice, since we just write to HERE and everything flows through
  nicely, as expected.

  Level words will be passed around as pairs at all points: the address (xt) and
  the level (L). Therefore the capacity of the level stack is half its number of
  cells, because each entry takes up two cells.
)

\ Allow us to use (L: to introduce comments
: (L: POSTPONE ( ; IMMEDIATE

\ Firstly, we need the level stack
32 ARRAY LSTACK
0 LSTACK VARIABLE! LSP

: LSTACK-EMPTY? 0 LSTACK LSP @ >= ;
: LSP++ ( -- a ) LSP PTR++ ;
: --LSP ( -- a ) LSP --PTR ;
: >LSP ( a -- ) LSP++ ! ;
: LSP> ( -- a ) --LSP @ ;
: LPUSH ( xt L -- ) (L: -- xt L ) SWAP >LSP >LSP ;
: LPOP ( -- xt L ) (L: xt L -- ) LSP> LSP> SWAP ;
: L@ ( -- a ) (L: a -- a ) LSP @ CELL - @ ;

\ Now, we need somewhere to store the EXPR-specific operators: its own
\ dictionary, essentially. We do this with four parallel arrays for the address xt,
\ level L, and name address a as a counted string.
256 CONSTANT LDICT-SIZE
LDICT-SIZE  ARRAY LDICT-XT
LDICT-SIZE CARRAY LDICT-L
LDICT-SIZE  ARRAY LDICT-A
VARIABLE LDICT-N

\ Store a new level word into the level dict
: (LEVEL) ( xt L addr u -- )
  CSTRING, ( xt L c-addr )
  LDICT-N @ ( xt L c-addr n )
  TUCK LDICT-A ! ( xt L n )
  TUCK LDICT-L C! ( xt n )
  TUCK LDICT-XT ! ( n )
  1+ LDICT-N ! ;

\ As above, but PARSE-NAME to get the next token instead of needing a string
: :LEVEL PARSE-NAME (LEVEL) ;

\ Find a level word in the level dict
: FIND-LEVEL ( addr u -- xt L true | addr u false )
  LDICT-N @ >R  \ stash the limit on the return stack
  0
  BEGIN DUP R@ < WHILE
    ( addr u i )
    \ stash i on the return stack to make dealing with the strings easier
    >R
    2DUP R@ LDICT-A @ COUNT  ( addr u addr u ai ui )
    S= IF                    ( addr u )
    2DROP R> DUP             ( i i )
    LDICT-XT @               ( i xt )
    SWAP LDICT-L C@          ( xt L )
    R> DROP T EXIT           ( xt L true )
    THEN
    R> 1+
  REPEAT DROP R> DROP F ;  \ remove i and the limit, add false

\ Shunting-yard algorithm
\ Pop and execute everything on LSTACK with a >= level to the input
\ Then, push the input
\ We use some variables to store the pending level word while we work
\ This means the function is *not* re-entrant. Probably fine!
VARIABLE SHUNT-XT
VARIABLE SHUNT-L
: (SHUNT) ( xt L -- )
  SHUNT-L ! SHUNT-XT !
  BEGIN
    LSTACK-EMPTY? IF F ELSE L@ SHUNT-L @ >= THEN
  WHILE
    LPOP DROP EXECUTE
  REPEAT
  SHUNT-XT @ SHUNT-L @ LPUSH ;

' + 1 :LEVEL +
' - 1 :LEVEL -

\ We can now hide the internals
(
HIDE LSTACK LSTACK-EMPTY?
HIDE LSP HIDE LSP++ HIDE --LSP HIDE >LSP HIDE LSP>
HIDE LPUSH HIDE LPOP HIDE L@
HIDE LDICT-SIZE HIDE LDICT-XT HIDE LDICT-L HIDE LDICT-A HIDE LDICT-N
HIDE SHUNT-XT HIDE SHUNT-L HIDE (SHUNT)
)
