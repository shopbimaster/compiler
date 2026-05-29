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
        Opt::runO1(mod);
        Opt::runO2(mod);
        Opt::runO3(mod);
        Opt::runP0(mod);
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
    Backend::TargetCodeGen cg;
    std::string asmCode = cg.generate(*mod);
    out << Opt::peepholeOptimize(asmCode);
}

void Compiler::emitAsmToFile(const std::string& sourcePath, const std::string& outputPath, OptLevel opt) {
    auto mod = compile(sourcePath);
    runOptPasses(mod.get(), opt);
    Backend::TargetCodeGen cg;
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("Cannot open output file: " + outputPath);
    }
    std::string asmCode = cg.generate(*mod);
    ofs << Opt::peepholeOptimize(asmCode);
}

} // namespace IR