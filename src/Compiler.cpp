#include "Compiler.h"

// 临时实现 - 待完善

Compiler::Compiler(const std::string& in, const std::string& out)
    : inputFile(in), outputFile(out)
    , errorReporter(std::make_unique<ErrorReporter>(in)) {
}

bool Compiler::compile() {
    Logger::getInstance().info("Starting compilation...");

    // TODO: 实现完整的编译流程
    // 1. Parse
    // 2. Generate IR
    // 3. Generate Assembly

    return runCompilePipeline();
}

bool Compiler::runCompilePipeline() {
    // 临时返回成功
    Logger::getInstance().info("Compile pipeline (placeholder)");
    return true;
}
