grammar Amex;

file: expr* EOF;

// Expressions
expr
    : '(' expr* ')'               # tupleExpr
    | '[' expr* ']'               # arrayExpr
    | '{' (expr expr)* '}'        # tableExpr
    | string                      # stringExpr
    | ':' token                   # keywordExpr
    | quote_expr                  # specialExpr
    | token                       # tokenExpr
    ;

// Special quoting forms: 'quote, ~quasiquote, ,unquote, ;splice
quote_expr
    : '\'' expr                   # quoteExpr
    | '~' expr                    # quasiquoteExpr
    | ',' expr                    # unquoteExpr
    | ';' expr                    # spliceExpr
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
