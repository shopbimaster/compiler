#include <iostream>
#include <string>
#include <filesystem>
#include "Compiler.h"
#include "utils/Logger.h"

void printUsage(const char* progName) {
    std::cerr << "Usage: " << progName << " [options] input.sy\n"
              << "Options:\n"
              << "  -o <file>    Output assembly file (default: a.s)\n"
              << "  -v           Verbose output\n"
              << "  -h, --help   Show this help message\n";
}

int main(int argc, char** argv) {
    std::string inputFile;
    std::string outputFile = "a.s";
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "-v") {
            verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            inputFile = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (inputFile.empty()) {
        std::cerr << "Error: No input file specified\n";
        printUsage(argv[0]);
        return 1;
    }

    if (!std::filesystem::exists(inputFile)) {
        std::cerr << "Error: Input file does not exist: " << inputFile << "\n";
        return 1;
    }

    if (verbose) {
        Logger::getInstance().setLogLevel(LogLevel::DEBUG);
    }

    Logger::getInstance().info("Compiling " + inputFile + " -> " + outputFile);

    Compiler compiler(inputFile, outputFile);
    bool success = compiler.compile();

    if (success) {
        Logger::getInstance().info("Compilation succeeded!");
        return 0;
    } else {
        Logger::getInstance().error("Compilation failed!");
        return 1;
    }
}
