#pragma once

#include <vector>
#include <string>
#include <memory>
#include <variant>
#include <cstdint>

namespace AST {

// 数据类型
enum class Type {
    INT,
    FLOAT,
    VOID
};

enum class BinOp {
    ADD, SUB, MUL, DIV, MOD,
    LT, GT, LE, GE, EQ, NE,
    AND, OR
};

enum class UnaryOp {
    PLUS, MINUS, NOT
};

class Visitor;

class Node {
public:
    virtual ~Node() = default;
    virtual void accept(Visitor& visitor) = 0;
    int line;
    int column;
};

class Expr : public Node {};
class Stmt : public Node {};
class Decl : public Node {};

// 数值表达式（整型或浮点型）
class Number : public Expr {
public:
    bool isFloat;
    int64_t intValue;
    float floatValue;
    
    Number(int64_t val, int l, int c) 
        : isFloat(false), intValue(val), floatValue(0.0f) 
        { line = l; column = c; }
    
    Number(float val, int l, int c) 
        : isFloat(true), intValue(0), floatValue(val) 
        { line = l; column = c; }
    
    void accept(Visitor& visitor) override;
};

class Identifier : public Expr {
public:
    std::string name;
    Identifier(const std::string& n, int l, int c) : name(n) { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class BinaryExpr : public Expr {
public:
    BinOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    BinaryExpr(BinOp o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, int line, int col)
        : op(o), left(std::move(l)), right(std::move(r)) { this->line = line; this->column = col; }
    void accept(Visitor& visitor) override;
};

class UnaryExpr : public Expr {
public:
    UnaryOp op;
    std::unique_ptr<Expr> operand;
    UnaryExpr(UnaryOp o, std::unique_ptr<Expr> e, int line, int col)
        : op(o), operand(std::move(e)) { this->line = line; this->column = col; }
    void accept(Visitor& visitor) override;
};

class CallExpr : public Expr {
public:
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    CallExpr(const std::string& name, std::vector<std::unique_ptr<Expr>> a, int l, int c)
        : callee(name), args(std::move(a)) { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class ArrayAccess : public Expr {
public:
    std::string name;
    std::vector<std::unique_ptr<Expr>> indices;
    ArrayAccess(const std::string& n, std::vector<std::unique_ptr<Expr>> idx, int l, int c)
        : name(n), indices(std::move(idx)) { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class Block : public Stmt {
public:
    std::vector<std::unique_ptr<Node>> items;
    explicit Block(std::vector<std::unique_ptr<Node>> items, int l, int c)
        : items(std::move(items)) { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class AssignStmt : public Stmt {
public:
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    AssignStmt(std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, int line, int col)
        : lhs(std::move(l)), rhs(std::move(r)) { this->line = line; this->column = col; }
    void accept(Visitor& visitor) override;
};

class IfStmt : public Stmt {
public:
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> thenStmt;
    std::unique_ptr<Stmt> elseStmt;
    IfStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> t, std::unique_ptr<Stmt> e, int l, int col)
        : cond(std::move(c)), thenStmt(std::move(t)), elseStmt(std::move(e)) { line = l; column = col; }
    void accept(Visitor& visitor) override;
};

class WhileStmt : public Stmt {
public:
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> body;
    WhileStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> b, int l, int col)
        : cond(std::move(c)), body(std::move(b)) { line = l; column = col; }
    void accept(Visitor& visitor) override;
};

class BreakStmt : public Stmt {
public:
    BreakStmt(int l, int c) { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class ContinueStmt : public Stmt {
public:
    ContinueStmt(int l, int c) { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class ReturnStmt : public Stmt {
public:
    std::unique_ptr<Expr> expr;
    explicit ReturnStmt(std::unique_ptr<Expr> e, int l, int c)
        : expr(std::move(e)) { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class ExprStmt : public Stmt {
public:
    std::unique_ptr<Expr> expr;
    explicit ExprStmt(std::unique_ptr<Expr> e, int l, int c)
        : expr(std::move(e)) { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class VarDecl : public Decl {
public:
    bool isConst;
    Type varType;
    std::string name;
    std::vector<std::unique_ptr<Expr>> dims;
    std::unique_ptr<Expr> init;
    VarDecl(bool isConst, Type type, const std::string& n, 
            std::vector<std::unique_ptr<Expr>> d, std::unique_ptr<Expr> i, int l, int c)
        : isConst(isConst), varType(type), name(n), dims(std::move(d)), init(std::move(i))
        { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class FuncParam {
public:
    Type paramType;
    std::string name;
    bool isArray;
    std::vector<std::unique_ptr<Expr>> dims;
    FuncParam(Type type, const std::string& n, bool isArr, 
              std::vector<std::unique_ptr<Expr>> d)
        : paramType(type), name(n), isArray(isArr), dims(std::move(d)) {}
};

class FuncDef : public Decl {
public:
    std::string name;
    Type returnType;
    std::vector<FuncParam> params;
    std::unique_ptr<Block> body;
    FuncDef(const std::string& n, Type retType, std::vector<FuncParam> p,
            std::unique_ptr<Block> b, int l, int c)
        : name(n), returnType(retType), params(std::move(p)), body(std::move(b))
        { line = l; column = c; }
    void accept(Visitor& visitor) override;
};

class CompilationUnit : public Node {
public:
    std::vector<std::unique_ptr<Decl>> decls;
    explicit CompilationUnit(std::vector<std::unique_ptr<Decl>> d)
        : decls(std::move(d)) {}
    void accept(Visitor& visitor) override;
};

class Visitor {
public:
    virtual ~Visitor() = default;
    virtual void visit(Number&) = 0;
    virtual void visit(Identifier&) = 0;
    virtual void visit(BinaryExpr&) = 0;
    virtual void visit(UnaryExpr&) = 0;
    virtual void visit(CallExpr&) = 0;
    virtual void visit(ArrayAccess&) = 0;
    virtual void visit(Block&) = 0;
    virtual void visit(AssignStmt&) = 0;
    virtual void visit(IfStmt&) = 0;
    virtual void visit(WhileStmt&) = 0;
    virtual void visit(BreakStmt&) = 0;
    virtual void visit(ContinueStmt&) = 0;
    virtual void visit(ReturnStmt&) = 0;
    virtual void visit(ExprStmt&) = 0;
    virtual void visit(VarDecl&) = 0;
    virtual void visit(FuncDef&) = 0;
    virtual void visit(CompilationUnit&) = 0;
};

}
