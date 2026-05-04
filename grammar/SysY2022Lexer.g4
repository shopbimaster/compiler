lexer grammar SysY2022Lexer;

Int: 'int';
Void: 'void';
Const: 'const';
If: 'if';
Else: 'else';
While: 'while';
Break: 'break';
Continue: 'continue';
Return: 'return';

LParen: '(';
RParen: ')';
LBracket: '[';
RBracket: ']';
LBrace: '{';
RBrace: '}';
Comma: ',';
Semicolon: ';';
Question: '?';
Colon: ':';

Plus: '+';
Minus: '-';
Star: '*';
Slash: '/';
Percent: '%';
Not: '!';
Lt: '<';
Gt: '>';
Le: '<=';
Ge: '>=';
Eq: '==';
Neq: '!=';
And: '&&';
Or: '||';
Assign: '=';

Identifier: [a-zA-Z_][a-zA-Z0-9_]*;

DecInt: [1-9][0-9]*;
OctInt: '0'[0-7]*;
HexInt: '0'[xX][0-9a-fA-F]+;

Whitespace: [ \t\r\n]+ -> skip;
LineComment: '//' ~[\r\n]* -> skip;
BlockComment: '/*' .*? '*/' -> skip;
