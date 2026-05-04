#pragma once

#include "AST.h"
#include "utils/Error.h"
#include <unordered_map>
#include <vector>
#include <string>

namespace AST {

struct SymbolInfo {
    enum class Type { VAR, CONST, FUNC };
    Type type;
    bool isArray;
    std::vector<int> dims;
    bool returnsInt;
    int paramCount;
};

class Scope {
private:
    std::unordered_map<std::string, SymbolInfo> symbols;
    Scope* parent;

public:
    explicit Scope(Scope* p = nullptr) : parent(p) {}

    bool define(const std::string& name, const SymbolInfo& info) {
        if (symbols.find(name) != symbols.end()) return false;
        symbols[name] = info;
        return true;
    }

    const SymbolInfo* lookup(const std::string& name) const {
        auto it = symbols.find(name);
        if (it != symbols.end()) return &it->second;
        if (parent) return parent->lookup(name);
        return nullptr;
    }

    Scope* getParent() const { return parent; }
};

class SemanticAnalyzer : public Visitor {
private:
    ErrorReporter& errorReporter;
    std::unique_ptr<Scope> currentScope;
    std::vector<std::string> breakScopes;
    std::vector<std::string> continueScopes;
    bool inLoop;

public:
    explicit SemanticAnalyzer(ErrorReporter& reporter);

    void analyze(CompilationUnit& cu);

    void visit(Number&) override;
    void visit(Identifier&) override;
    void visit(BinaryExpr&) override;
    void visit(UnaryExpr&) override;
    void visit(CallExpr&) override;
    void visit(ArrayAccess&) override;
    void visit(Block&) override;
    void visit(AssignStmt&) override;
    void visit(IfStmt&) override;
    void visit(WhileStmt&) override;
    void visit(BreakStmt&) override;
    void visit(ContinueStmt&) override;
    void visit(ReturnStmt&) override;
    void visit(ExprStmt&) override;
    void visit(VarDecl&) override;
    void visit(FuncDef&) override;
    void visit(CompilationUnit&) override;

private:
    void enterScope();
    void exitScope();
    void reportError(const std::string& msg, int line, int col);
};

}
