parser grammar SysY2022Parser;

options {
    tokenVocab=SysY2022Lexer;
}

compilationUnit: (decl | funcDef)* EOF;

decl: constDecl | varDecl;

constDecl: Const bType constDef (Comma constDef)* Semicolon;

constDef: Identifier (LBracket constExp RBracket)* Assign constInitVal;

constInitVal: constExp | LBrace (constInitVal (Comma constInitVal)*)? RBrace;

varDecl: bType varDef (Comma varDef)* Semicolon;

varDef: Identifier (LBracket constExp RBracket)* (Assign initVal)?;

initVal: exp | LBrace (initVal (Comma initVal)*)? RBrace;

bType: Int;

funcDef: funcType Identifier LParen (funcFParams)? RParen block;

funcType: Void | Int;

funcFParams: funcFParam (Comma funcFParam)*;

funcFParam: bType Identifier (LBracket RBracket (LBracket constExp RBracket)*)?;

block: LBrace (blockItem)* RBrace;

blockItem: decl | stmt;

stmt:
    lVal Assign exp Semicolon
    | (exp)? Semicolon
    | block
    | If LParen cond RParen stmt (Else stmt)?
    | While LParen cond RParen stmt
    | Break Semicolon
    | Continue Semicolon
    | Return (exp)? Semicolon
    ;

exp: addExp;

cond: lOrExp;

lVal: Identifier (LBracket exp RBracket)*;

primaryExp: LParen exp RParen | lVal | number;

number: DecInt | OctInt | HexInt;

unaryExp: primaryExp | Identifier LParen (funcRParams)? RParen | unaryOp unaryExp;

unaryOp: Plus | Minus | Not;

funcRParams: exp (Comma exp)*;

mulExp: unaryExp (mulOp unaryExp)*;

mulOp: Star | Slash | Percent;

addExp: mulExp (addOp mulExp)*;

addOp: Plus | Minus;

relExp: addExp (relOp addExp)*;

relOp: Lt | Gt | Le | Ge;

eqExp: relExp (eqOp relExp)*;

eqOp: Eq | Neq;

lAndExp: eqExp (And eqExp)*;

lOrExp: lAndExp (Or lAndExp)*;

constExp: addExp;
