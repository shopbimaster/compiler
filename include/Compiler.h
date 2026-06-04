#pragma once

#include "ir/IRBuilder.h"
#include <string>
#include <memory>

namespace IR {

enum class OptLevel {
    O0,
    O1,
    O2,
    O3,
    OALL
};

class Compiler {
public:
    Compiler() = default;

    std::unique_ptr<Module> compile(const std::string& sourcePath);

    void emitIR(const std::string& sourcePath, std::ostream& out, OptLevel opt = OptLevel::O0);

    void emitIRToFile(const std::string& sourcePath, const std::string& outputPath, OptLevel opt = OptLevel::O0);

    void emitAsm(const std::string& sourcePath, std::ostream& out, OptLevel opt = OptLevel::OALL);

    void emitAsmToFile(const std::string& sourcePath, const std::string& outputPath, OptLevel opt = OptLevel::OALL);
};

} // namespace IR