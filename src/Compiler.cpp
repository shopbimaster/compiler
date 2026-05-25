#include "Compiler.h"
#include "backend/TargetCodeGen.h"
#include <fstream>
#include <iostream>

namespace IR {

std::unique_ptr<Module> Compiler::compile(const std::string& sourcePath) {
    IRBuilder builder;
    return builder.compile(sourcePath);
}

void Compiler::emitIR(const std::string& sourcePath, std::ostream& out) {
    auto mod = compile(sourcePath);
    out << mod->dump();
}

void Compiler::emitIRToFile(const std::string& sourcePath, const std::string& outputPath) {
    auto mod = compile(sourcePath);
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("Cannot open output file: " + outputPath);
    }
    ofs << mod->dump();
}

void Compiler::emitAsm(const std::string& sourcePath, std::ostream& out) {
    auto mod = compile(sourcePath);
    Backend::TargetCodeGen cg;
    out << cg.generate(*mod);
}

void Compiler::emitAsmToFile(const std::string& sourcePath, const std::string& outputPath) {
    auto mod = compile(sourcePath);
    Backend::TargetCodeGen cg;
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("Cannot open output file: " + outputPath);
    }
    ofs << cg.generate(*mod);
}

} // namespace IR