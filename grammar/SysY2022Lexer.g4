lexer grammar SysY2022Lexer;

// ===== 基础字符 =====
fragment LETTER: [a-zA-Z];
fragment DIGIT:  [0-9];
fragment WORD:   LETTER | DIGIT | '_';

// ===== 关键字 =====
INT:      'int';
FLOAT:    'float';
VOID:     'void';
CONST:    'const';
IF:       'if';
ELSE:     'else';
WHILE:    'while';
BREAK:    'break';
CONTINUE: 'continue';
RETURN:   'return';

// 《前端+显式长度》 显式长度向量关键字（vec4/vec8/vec16/...）
// 放在 IDENTIFIER 前：max munch 下 'vec4' 长度 4 == IDENTIFIER 长度 4，
// 平局由规则顺序决定 → VEC_N 胜（成为关键字）；'vec4x' 长度 5 > VEC_N 的 4
// → IDENTIFIER 胜（避免误吞 vec4x 变量名）。长度任意正整数，无需为每个长度
// 新增关键字。
VEC_N:    'vec' DIGIT+;

// ===== 分隔符 =====
L_PAREN:    '(';
R_PAREN:    ')';
L_BRACKET:  '[';
R_BRACKET:  ']';
L_BRACE:    '{';
R_BRACE:    '}';
COMMA:      ',';
SEMICOLON:  ';';
QUESTION:   '?';
COLON:      ':';

// ===== 算术运算符 =====
PLUS:   '+';
MINUS:  '-';
STAR:   '*';
DIV:    '/';
MOD:    '%';

// ===== 逻辑运算符 =====
NOT: '!';
AND: '&&';
OR:  '||';

// ===== 关系运算符 =====
LT: '<';
GT: '>';
LE: '<=';
GE: '>=';
EQ: '==';
NE: '!=';

// ===== 赋值运算符 =====
ASSIGN: '=';

// ===== 标识符 =====
IDENTIFIER: ('_' | LETTER) WORD*;

// ===== 整数常量 =====
INTCONST:
      '0'
    | ([1-9] DIGIT*)
    | ('0' [1-7] [0-7]*)
    | ('0' ('x' | 'X') ([1-9a-fA-F] [0-9a-fA-F]*));

// ===== 浮点常量 =====
FLOATCONST:
      (DIGIT+ '.' DIGIT* | '.' DIGIT+) (('e' | 'E') ('+' | '-')? DIGIT+)?
    | DIGIT+ (('e' | 'E') ('+' | '-')? DIGIT+)
    | ('0' ('x' | 'X')) 
        (
            ([0-9a-fA-F]+ '.' [0-9a-fA-F]* | '.' [0-9a-fA-F]+)
            | ([0-9a-fA-F]+)
        )
        (('p' | 'P') ('+' | '-')? DIGIT+);

// ===== 空白和注释 =====
WHITESPACE:    [ \t\r\n]+ -> skip;
LINE_COMMENT:  '//' ~[\r\n]* -> skip;
BLOCK_COMMENT: '/*' .*? '*/' -> skip;
