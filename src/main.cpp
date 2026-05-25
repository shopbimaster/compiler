#include <iostream>
#include <cstring>
#include "Compiler.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: sysyc <input.sy> [-S] [-o <output>]\n";
        std::cerr << "       sysyc -S <input.sy> [-o <output>]\n";
        return 1;
    }

    std::string inputPath;
    std::string outputPath;
    bool emitAssembly = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "-S") {
            emitAssembly = true;
        } else if (inputPath.empty()) {
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
                compiler.emitAsm(inputPath, std::cout);
            } else {
                compiler.emitAsmToFile(inputPath, outputPath);
                std::cout << "Assembly written to " << outputPath << "\n";
            }
        } else {
            if (outputPath.empty()) {
                compiler.emitIR(inputPath, std::cout);
            } else {
                compiler.emitIRToFile(inputPath, outputPath);
                std::cout << "IR written to " << outputPath << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}