#include <iostream>
#include <cstring>
#include "Compiler.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: sysyc <input.sy> [-S] [-o <output>] [-O0|-O1|-O2|-O3|-Oall]\n";
        std::cerr << "       sysyc -S <input.sy> [-o <output>] [-O0|-O1|-O2|-O3|-Oall]\n";
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
        } else if (arg == "-O0") {
            optLevel = IR::OptLevel::O0;
        } else if (arg == "-O1") {
            optLevel = IR::OptLevel::O1;
        } else if (arg == "-O2") {
            optLevel = IR::OptLevel::O2;
        } else if (arg == "-O3") {
            optLevel = IR::OptLevel::O3;
        } else if (arg == "-Oall") {
            optLevel = IR::OptLevel::OALL;
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