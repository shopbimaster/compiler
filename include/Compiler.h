#pragma once

#include "ir/IRBuilder.h"
#include <string>
#include <memory>

namespace IR {

class Compiler {
public:
    Compiler() = default;

    // 编译 .sy 源文件，返回 IR Module
    std::unique_ptr<Module> compile(const std::string& sourcePath);

    // 输出 IR 到 ostream
    void emitIR(const std::string& sourcePath, std::ostream& out);

    // 输出 IR 到文件
    void emitIRToFile(const std::string& sourcePath, const std::string& outputPath);
};

} // namespace IR