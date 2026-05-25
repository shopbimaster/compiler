#pragma once

#include "ir/IRBuilder.h"
#include <string>
#include <memory>

namespace IR {

class Compiler {
public:
    Compiler() = default;

    std::unique_ptr<Module> compile(const std::string& sourcePath);

    void emitIR(const std::string& sourcePath, std::ostream& out);

    void emitIRToFile(const std::string& sourcePath, const std::string& outputPath);

    void emitAsm(const std::string& sourcePath, std::ostream& out);

    void emitAsmToFile(const std::string& sourcePath, const std::string& outputPath);
};

} // namespace IR