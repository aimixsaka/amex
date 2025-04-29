grammar Amex;

file: expr* EOF;

// Expressions
expr
    : tuple_expr
    | array_expr
    | table_expr
    | keyword_expr
    | quote_unquote_expr
    | string
    | token
    ;

tuple_expr: '(' expr* ')';
array_expr: '[' expr* ']';
table_expr: '{' (expr expr)* '}';
keyword_expr: ':' token;
quote_unquote_expr
    : '\'' expr
    | '~' expr
    | ',' expr
    | ';' expr
    ;

string
    : STRING
    ;

token
    : NIL
    | BOOL
    | NUMBER
    | SYMBOL
    ;

NIL: 'nil';
BOOL: 'true' | 'false';

NUMBER
    : ('+' | '-')? DIGIT+ ('.' DIGIT+)? ([eE] ('+' | '-')? DIGIT+)?
    ;

SYMBOL
    : SYMBOL_START SYMBOL_REST*
    ;

// Literals and Escaping
STRING
    : '"' (ESC | ~["\\])* '"'
    ;

fragment ESC
    : '\\' [nrtf0"'z]
    ;

// Symbol rules
fragment SYMBOL_START
    : [a-zA-Z<+@#-&*_^!]
    ;

fragment SYMBOL_REST
    : SYMBOL_START | DIGIT
    ;

fragment DIGIT : [0-9];

// Comments and Whitespace
COMMENT
    : '#' ~[\r\n]* -> skip
    ;

WS
    : [ \t\r\n]+ -> skip
    ;
