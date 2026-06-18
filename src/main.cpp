#include <iostream>
#include <cstring>
#include "Compiler.h"

// ================================================================
// 命令行参数与优化级别映射
// ================================================================
// 测评服务器仅支持 -O1 这一个优化选项，因此：
//   -O1     → OALL  (全部优化：O1+O2+O3，但不含 P0/P3)
//   -O0     → O0    (无优化)
// 小写字母用于本地调试，精确控制各级优化：
//   -o0     → O0    (无优化)
//   -o1     → O1    (仅 O1：CF+DCE+CSE+LICM)
//   -o2     → O2    (O1 + 内联 + 额外 CSE/LICM)
//   -o3     → O3    (O1+O2 + 代数化简/循环交换/展开/尾递归)
// ================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: compiler -S -o <output> <input.sy> [-O1]\n";
        std::cerr << "  -O1  (测评服务器使用) 启用全部优化\n";
        std::cerr << "  -o0/-o1/-o2/-o3  (本地调试) 精确控制优化级别\n";
        return 1;
    }

    std::string inputPath;
    std::string outputPath;
    bool emitAssembly = false;
    IR::OptLevel optLevel = IR::OptLevel::O0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "-S") {
            emitAssembly = true;
        // ── 测评服务器级别（大写） ──
        } else if (arg == "-O0") {
            optLevel = IR::OptLevel::O0;
        } else if (arg == "-O1") {
            // 测评服务器仅支持 -O1，映射到全部优化
            optLevel = IR::OptLevel::OALL;
        // ── 本地调试级别（小写） ──
        } else if (arg == "-o0") {
            optLevel = IR::OptLevel::O0;
        } else if (arg == "-o1") {
            optLevel = IR::OptLevel::O1;
        } else if (arg == "-o2") {
            optLevel = IR::OptLevel::O2;
        } else if (arg == "-o3") {
            optLevel = IR::OptLevel::O3;
        } else if (arg[0] != '-' && inputPath.empty()) {
            inputPath = arg;
        }
    }

    if (inputPath.empty()) {
        std::cerr << "Error: no input file specified\n";
        return 1;
    }

    try {
        IR::Compiler compiler;

        if (emitAssembly) {
            if (outputPath.empty()) {
                compiler.emitAsm(inputPath, std::cout, optLevel);
            } else {
                compiler.emitAsmToFile(inputPath, outputPath, optLevel);
                std::cout << "Assembly written to " << outputPath << "\n";
            }
        } else {
            if (outputPath.empty()) {
                compiler.emitIR(inputPath, std::cout, optLevel);
            } else {
                compiler.emitIRToFile(inputPath, outputPath, optLevel);
                std::cout << "IR written to " << outputPath << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}