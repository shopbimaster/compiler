lexer grammar SysY2022Lexer;

// 关键字
INT: 'int';
FLOAT: 'float';
VOID: 'void';
CONST: 'const';
IF: 'if';
ELSE: 'else';
WHILE: 'while';
BREAK: 'break';
CONTINUE: 'continue';
RETURN: 'return';

// 运算符和分隔符
L_PAREN: '(';
R_PAREN: ')';
L_BRACKET: '[';
R_BRACKET: ']';
L_BRACE: '{';
R_BRACE: '}';
COMMA: ',';
SEMICOLON: ';';
QUESTION: '?';
COLON: ':';

// 算术运算符
PLUS: '+';
MINUS: '-';
STAR: '*';
DIV: '/';
MOD: '%';

// 逻辑运算符
NOT: '!';
AND: '&&';
OR: '||';

// 关系运算符
LT: '<';
GT: '>';
LE: '<=';
GE: '>=';
EQ: '==';
NE: '!=';

// 赋值
ASSIGN: '=';

// 标识符
IDENTIFIER: IdentifierNondigit (IdentifierNondigit | Digit)*;

fragment
IdentifierNondigit: [a-zA-Z_];

fragment
Digit: [0-9];

// 整型常量
INTCONST: DecimalConst | OctalConst | HexadecimalConst;

fragment
DecimalConst: NonzeroDigit Digit*;

fragment
OctalConst: '0' OctalDigit*;

fragment
HexadecimalConst: HexadecimalPrefix HexadecimalDigit+;

fragment
HexadecimalPrefix: '0' [xX];

fragment
NonzeroDigit: [1-9];

fragment
OctalDigit: [0-7];

fragment
HexadecimalDigit: [0-9a-fA-F];

// 浮点常量
FLOATCONST: (DecimalFloatingConst | HexadecimalFloatingConst);

fragment
DecimalFloatingConst: 
    (FractionalConstant ExponentPart? | DigitSequence ExponentPart);

fragment
HexadecimalFloatingConst:
    HexadecimalPrefix (HexadecimalFractionalConstant | HexadecimalDigitSequence) BinaryExponentPart;

fragment
FractionalConstant:
    DigitSequence? '.' DigitSequence
    | DigitSequence '.';

fragment
ExponentPart: [eE] Sign? DigitSequence;

fragment
Sign: [+-];

fragment
DigitSequence: Digit+;

fragment
HexadecimalFractionalConstant:
    HexadecimalDigitSequence? '.' HexadecimalDigitSequence
    | HexadecimalDigitSequence '.';

fragment
BinaryExponentPart: [pP] Sign? DigitSequence;

fragment
HexadecimalDigitSequence: HexadecimalDigit+;

// 空白字符
WHITESPACE: [ \t\r\n]+ -> skip;

// 注释
LINE_COMMENT: '//' ~[\r\n]* -> skip;
BLOCK_COMMENT: '/*' .*? '*/' -> skip;
