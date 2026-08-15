parser grammar SysY2022Parser;

options {
    tokenVocab=SysY2022Lexer;
}

// 编译单元
compilationUnit: (decl | funcDef)* EOF;

// 声明
decl: constDecl | varDecl | vectorDecl;

// 向量声明：vector<T> name [ constLen ] ;（T ∈ {int, float}，长度为编译期常量）
vectorDecl:
    VECTOR LT bType GT IDENTIFIER
    L_BRACKET constExp R_BRACKET SEMICOLON;

// 常量声明
constDecl: CONST bType constDef (COMMA constDef)* SEMICOLON;

// 基本类型
bType: INT | FLOAT;

// 常量定义
constDef: IDENTIFIER (L_BRACKET constExp R_BRACKET)* ASSIGN constInitVal;

// 常量初值
constInitVal: 
    constExp 
    | L_BRACE (constInitVal (COMMA constInitVal)*)? R_BRACE;

// 变量声明
varDecl: bType varDef (COMMA varDef)* SEMICOLON;

// 变量定义
varDef: 
    IDENTIFIER (L_BRACKET constExp R_BRACKET)*
    | IDENTIFIER (L_BRACKET constExp R_BRACKET)* ASSIGN initVal;

// 变量初值
initVal: 
    exp 
    | L_BRACE (initVal (COMMA initVal)*)? R_BRACE;

// 函数定义
funcDef: funcType IDENTIFIER L_PAREN (funcFParams)? R_PAREN block;

// 函数类型
funcType: VOID | INT | FLOAT;

// 函数形参表
funcFParams: funcFParam (COMMA funcFParam)*;

// 函数形参
funcFParam: bType IDENTIFIER (L_BRACKET R_BRACKET (L_BRACKET exp R_BRACKET)*)?;

// 语句块
block: L_BRACE blockItem* R_BRACE;

// 语句块项
blockItem: decl | stmt;

// 语句
stmt: 
    lVal ASSIGN exp SEMICOLON
    | exp? SEMICOLON
    | block
    | IF L_PAREN cond R_PAREN stmt (ELSE stmt)?
    | WHILE L_PAREN cond R_PAREN stmt
    | BREAK SEMICOLON
    | CONTINUE SEMICOLON
    | RETURN exp? SEMICOLON;

// 表达式
exp: addExp;

// 条件表达式
cond: lOrExp;

// 左值表达式
lVal: IDENTIFIER (L_BRACKET exp R_BRACKET)*;

// 基本表达式
primaryExp: L_PAREN exp R_PAREN | lVal | number;

// 数值
number: INTCONST | FLOATCONST;

// 一元表达式
unaryExp: 
    primaryExp
    | IDENTIFIER L_PAREN (funcRParams)? R_PAREN
    | unaryOp unaryExp;

// 单目运算符
unaryOp: PLUS | MINUS | NOT;

// 函数实参表
funcRParams: exp (COMMA exp)*;

// 乘除模表达式
mulExp:
    unaryExp
    | mulExp (STAR | DIV | MOD) unaryExp;

// 加减表达式
addExp:
    mulExp
    | addExp (PLUS | MINUS) mulExp;

// 关系表达式
relExp:
    addExp
    | relExp (LT | GT | LE | GE) addExp;

// 相等性表达式
eqExp:
    relExp
    | eqExp (EQ | NE) relExp;

// 逻辑与表达式
lAndExp:
    eqExp
    | lAndExp AND eqExp;

// 逻辑或表达式
lOrExp:
    lAndExp
    | lOrExp OR lAndExp;

// 常量表达式
constExp: addExp;
