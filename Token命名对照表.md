# SysY2022 Token 命名对照表

## 一、命名规范
- 所有 token 全大写
- 表示左右方向的 L/R 后添加下划线分隔

---

## 二、完整对照表

### 2.1 关键字
| Token | 字符串 |
|-------|-------|
| INT | 'int' |
| FLOAT | 'float' |
| VOID | 'void' |
| CONST | 'const' |
| IF | 'if' |
| ELSE | 'else' |
| WHILE | 'while' |
| BREAK | 'break' |
| CONTINUE | 'continue' |
| RETURN | 'return' |

### 2.2 分隔符
| Token | 字符串 |
|-------|-------|
| L_PAREN | '(' |
| R_PAREN | ')' |
| L_BRACKET | '[' |
| R_BRACKET | ']' |
| L_BRACE | '{' |
| R_BRACE | '}' |
| COMMA | ',' |
| SEMICOLON | ';' |
| QUESTION | '?' |
| COLON | ':' |

### 2.3 算术运算符
| Token | 字符串 |
|-------|-------|
| PLUS | '+' |
| MINUS | '-' |
| STAR | '*' |
| DIV | '/' |
| MOD | '%' |

### 2.4 逻辑运算符
| Token | 字符串 |
|-------|-------|
| NOT | '!' |
| AND | '&&' |
| OR | '||' |

### 2.5 关系运算符
| Token | 字符串 |
|-------|-------|
| LT | '<' |
| GT | '>' |
| LE | '<=' |
| GE | '>=' |
| EQ | '==' |
| NE | '!=' |

### 2.6 赋值运算符
| Token | 字符串 |
|-------|-------|
| ASSIGN | '=' |

### 2.7 标识符和常量
| Token | 说明 |
|-------|------|
| IDENTIFIER | 标识符 |
| INTCONST | 整数常量 |
| FLOATCONST | 浮点常量 |

### 2.8 空白和注释
| Token | 说明 |
|-------|------|
| WHITESPACE | 空白字符 (跳过) |
| LINE_COMMENT | '//' 注释 (跳过) |
| BLOCK_COMMENT | '/*...*/' 注释 (跳过) |

---

## 三、统计
- 关键字：10 个
- 分隔符：10 个
- 算术运算符：5 个
- 逻辑运算符：3 个
- 关系运算符：6 个
- 赋值运算符：1 个
- 标识符和常量：3 个
- 空白和注释：3 个

**总计：41 个 Token**
