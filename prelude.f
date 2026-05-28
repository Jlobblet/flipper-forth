: [CHAR] CHAR LIT-COMPILE ; IMMEDIATE
: ( [CHAR] ) PARSE 2DROP ; IMMEDIATE
: \ 10 PARSE 2DROP ; IMMEDIATE

\ Now we have comments!

\ Words for entering compile mode for immediate execution
: [: :NONAME ; IMMEDIATE
: ;] POSTPONE EXIT POSTPONE [ EXECUTE ; IMMEDIATE

\ Cheap word for creating aliases
: ALIAS ( "newname" "oldname" -- ) CREATE ' , DOES> @ EXECUTE ;
ALIAS NB. \

: ? ( addr -- ) @ . ;
: CELLS ( n -- n_c ) CELL * ;

: VARIABLE! CREATE , ;
: VARIABLE CREATE 0 , ;
: CONSTANT CREATE , DOES> @ ;
: ARRAY CREATE CELLS ALLOT DOES> SWAP CELLS + ;

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
