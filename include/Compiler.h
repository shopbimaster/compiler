#pragma once

#include <string>
#include <memory>
#include "utils/Error.h"
#include "utils/Logger.h"
#include "ir/IR.h"
#include "ir/IRBuilder.h"
#include "backend/TargetCodeGen.h"
#include "backend/RegisterAllocator.h"
#include "backend/PeepholeOptimizer.h"

#include "antlr4-runtime.h"
#include "SysY2022Lexer.h"
#include "SysY2022Parser.h"

class Compiler {
private:
    std::string inputFile;
    std::string outputFile;
    std::unique_ptr<ErrorReporter> errorReporter;

public:
    Compiler(const std::string& in, const std::string& out);
    bool compile();

private:
    // 词法和语法分析 - 返回 ParseTree
    std::unique_ptr<SysY2022Parser::CompilationUnitContext> parse(antlr4::ANTLRInputStream& input);

    // 直接从 ParseTree 生成 IR
    std::unique_ptr<IR::Module> generateIR(SysY2022Parser::CompilationUnitContext* ctx);

    // 后端处理
    std::string generateAssembly(IR::Module& module);

    bool writeOutput(const std::string& asmCode);
};
