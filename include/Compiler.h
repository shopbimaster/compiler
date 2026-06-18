#pragma once

#include "ir/IRBuilder.h"
#include <string>
#include <memory>

namespace IR {

enum class OptLevel {
    O0,     // 无优化
    O1,     // 仅 O1 优化 (CF+DCE+CSE+LICM)
    O2,     // O1 + 内联 + 额外CSE/LICM
    O3,     // O1+O2 + 代数化简/循环交换/展开/尾递归
    OALL    // 全部优化 (O1+O2+O3，不含P0/P3) — 对应命令行 -O1（测评服务器唯一支持的优化选项）
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