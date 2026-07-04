#include "Compiler.h"
#include "backend/TargetCodeGen.h"
#include "opt/Optimizer.h"
#include <fstream>
#include <iostream>

namespace IR {

static void runOptPasses(Module* mod, OptLevel opt) {
    switch (opt) {
    case OptLevel::O1:
        Opt::runO1(mod);
        break;
    case OptLevel::O2:
        Opt::runO1(mod);
        Opt::runO2(mod);
        break;
    case OptLevel::O3:
        Opt::runO1(mod);
        Opt::runO2(mod);
        Opt::runO3(mod);
        break;
    case OptLevel::OALL:
        // OALL = O1+O2+O3+P0，对应命令行 -O1（测评服务器唯一支持的优化选项）
        Opt::runO1(mod);
        Opt::runO2(mod);
        Opt::runO3(mod);
        Opt::runP0(mod);
        // Opt::runP3(mod);  // TODO: 与Mem2Reg+PhiLowering交互导致SEGFAULT，待修复
        break;
    case OptLevel::O0:
    default:
        break;
    }
}

std::unique_ptr<Module> Compiler::compile(const std::string& sourcePath) {
    IRBuilder builder;
    return builder.compile(sourcePath);
}

void Compiler::emitIR(const std::string& sourcePath, std::ostream& out, OptLevel opt) {
    auto mod = compile(sourcePath);
    runOptPasses(mod.get(), opt);
    out << mod->dump();
}

void Compiler::emitIRToFile(const std::string& sourcePath, const std::string& outputPath, OptLevel opt) {
    auto mod = compile(sourcePath);
    runOptPasses(mod.get(), opt);
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("Cannot open output file: " + outputPath);
    }
    ofs << mod->dump();
}

void Compiler::emitAsm(const std::string& sourcePath, std::ostream& out, OptLevel opt) {
    auto mod = compile(sourcePath);
    runOptPasses(mod.get(), opt);
    // 在代码生成前降级 PHI 指令（如果 Mem2Reg 引入了 PHI）
    Opt::phiLowering(mod.get());
    Backend::TargetCodeGen cg;
    std::string asmCode = cg.generate(*mod);
    out << Opt::peepholeOptimize(asmCode);
}

void Compiler::emitAsmToFile(const std::string& sourcePath, const std::string& outputPath, OptLevel opt) {
    auto mod = compile(sourcePath);
    runOptPasses(mod.get(), opt);
    // 在代码生成前降级 PHI 指令（如果 Mem2Reg 引入了 PHI）
    Opt::phiLowering(mod.get());
    Backend::TargetCodeGen cg;
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("Cannot open output file: " + outputPath);
    }
    std::string asmCode = cg.generate(*mod);
    ofs << Opt::peepholeOptimize(asmCode);
}

} // namespace IR