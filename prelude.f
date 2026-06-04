: [CHAR] CHAR LIT-COMPILE ; IMMEDIATE
: (
  1 BEGIN
    KEY DUP [CHAR] ( =
    IF DROP 1 +
    ELSE [CHAR] ) = IF 1 - THEN THEN
  DUP 0 = UNTIL
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

\ Print a string immediately
: ." POSTPONE S" STATE @ IF POSTPONE TYPE ELSE TYPE THEN ; IMMEDIATE

\ Some string printing utilities now
0X20 CONSTANT BL ;
: SPACE BL EMIT ;
: CR 0XA EMIT ;
\ Print n spaces
: SPACES ( n -- ) BEGIN DUP WHILE SPACE 1 - REPEAT DROP ;

: 0= ( n -- ? ) 0 = ;
: 0< ( n -- ? ) 0 SWAP < ;
: 0> ( n -- ? ) 0 SWAP > ;

: 1+ ( n -- 1+n ) 1 + ;
ALIAS +1 1+
: 1- ( n -- 1-n ) 1 SWAP - ;
: 2* ( n -- 2n ) 2 * ;
: 2/ ( n -- 2/n )2 SWAP / ;
: /2 ( n -- n/2 ) 2 / ;
: NEGATE ( n -- -n ) 0 SWAP - ;
: ABS ( n -- |n| ) DUP 0 < IF NEGATE THEN ;

\ Constants for Boolean flags
0 CONSTANT F
-1 CONSTANT T

: SQUARE ( n -- n^2 ) DUP * ;
: QUAD ( n -- n^4 ) SQUARE SQUARE ;

: FAC ( n -- n! )
  1 SWAP
  BEGIN DUP
  WHILE TUCK * SWAP 1 -
  REPEAT DROP ;

: FIB ( n -- F_n )
  1 0 ROT
  BEGIN DUP
  WHILE -ROT SWAP TUCK + SWAP ROT 1 -
  REPEAT DROP NIP ;
