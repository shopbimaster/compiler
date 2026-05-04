#pragma once

#include <string>
#include <memory>
#include "utils/Error.h"
#include "utils/Logger.h"
#include "frontend/AST.h"
#include "frontend/ASTBuilder.h"
#include "frontend/SemanticAnalyzer.h"
#include "ir/IR.h"
#include "ir/IRBuilder.h"
#include "backend/TargetCodeGen.h"
#include "backend/RegisterAllocator.h"
#include "backend/PeepholeOptimizer.h"

class Compiler {
private:
    std::string inputFile;
    std::string outputFile;
    std::unique_ptr<ErrorReporter> errorReporter;

public:
    Compiler(const std::string& in, const std::string& out);
    bool compile();

private:
    std::unique_ptr<AST::CompilationUnit> parse();
    bool analyzeSemantics(AST::CompilationUnit& cu);
    std::unique_ptr<IR::Module> generateIR(AST::CompilationUnit& cu);
    std::string generateAssembly(IR::Module& module);
    bool writeOutput(const std::string& asmCode);
};
