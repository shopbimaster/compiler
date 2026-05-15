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

// 注意：ANTLR4 相关头文件不在 include/ 目录中
// 它们将在编译时从生成目录引用，或者在 Compiler.cpp 中引用

class Compiler {
private:
    std::string inputFile;
    std::string outputFile;
    std::unique_ptr<ErrorReporter> errorReporter;

public:
    Compiler(const std::string& in, const std::string& out);
    bool compile();

private:
    // 编译阶段 - 声明在这里，实现在 Compiler.cpp 中
    bool runCompilePipeline();
};
