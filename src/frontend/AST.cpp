#include "frontend/AST.h"

namespace AST {

void Number::accept(Visitor& visitor) { visitor.visit(*this); }
void Identifier::accept(Visitor& visitor) { visitor.visit(*this); }
void BinaryExpr::accept(Visitor& visitor) { visitor.visit(*this); }
void UnaryExpr::accept(Visitor& visitor) { visitor.visit(*this); }
void CallExpr::accept(Visitor& visitor) { visitor.visit(*this); }
void ArrayAccess::accept(Visitor& visitor) { visitor.visit(*this); }
void Block::accept(Visitor& visitor) { visitor.visit(*this); }
void AssignStmt::accept(Visitor& visitor) { visitor.visit(*this); }
void IfStmt::accept(Visitor& visitor) { visitor.visit(*this); }
void WhileStmt::accept(Visitor& visitor) { visitor.visit(*this); }
void BreakStmt::accept(Visitor& visitor) { visitor.visit(*this); }
void ContinueStmt::accept(Visitor& visitor) { visitor.visit(*this); }
void ReturnStmt::accept(Visitor& visitor) { visitor.visit(*this); }
void ExprStmt::accept(Visitor& visitor) { visitor.visit(*this); }
void VarDecl::accept(Visitor& visitor) { visitor.visit(*this); }
void FuncDef::accept(Visitor& visitor) { visitor.visit(*this); }
void CompilationUnit::accept(Visitor& visitor) { visitor.visit(*this); }

}
