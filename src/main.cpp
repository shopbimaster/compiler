#include <iostream>
#include "Compiler.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: sysyc <input.sy> [-o <output.ir>]\n";
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputPath;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        }
    }

    try {
        IR::Compiler compiler;

        if (outputPath.empty()) {
            compiler.emitIR(inputPath, std::cout);
        } else {
            compiler.emitIRToFile(inputPath, outputPath);
            std::cout << "IR written to " << outputPath << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}