#include "opt/Optimizer.h"

#include <iostream>
#include <string>

namespace {

bool expectContains(const std::string& text, const std::string& needle,
                    const char* message) {
    if (text.find(needle) != std::string::npos) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

bool expectNotContains(const std::string& text, const std::string& needle,
                       const char* message) {
    if (text.find(needle) == std::string::npos) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

} // namespace

int main() {
    bool passed = true;

    const std::string floatLoad =
        "  la t0, global_float\n"
        "  flw ft0, 0(t0)\n"
        "  li t0, 0\n"
        "  ret\n";
    const std::string floatResult = Opt::peepholeOptimize(floatLoad);
    passed &= expectContains(floatResult, "la t0, global_float",
                             "flw symbolic load lost its address register");
    passed &= expectContains(floatResult, "flw ft0, 0(t0)",
                             "flw symbolic load was rewritten");

    const std::string doubleLoad =
        "  la t0, global_double\n"
        "  fld ft1, 8(t0)\n"
        "  li t0, 0\n"
        "  ret\n";
    const std::string doubleResult = Opt::peepholeOptimize(doubleLoad);
    passed &= expectContains(doubleResult, "la t0, global_double",
                             "fld symbolic load lost its address register");
    passed &= expectContains(doubleResult, "fld ft1, 8(t0)",
                             "fld symbolic load was rewritten");

    const std::string integerLoad =
        "  la t0, global_int\n"
        "  lw t1, 0(t0)\n"
        "  li t0, 0\n"
        "  ret\n";
    const std::string integerResult = Opt::peepholeOptimize(integerLoad);
    passed &= expectNotContains(integerResult, "la t0, global_int",
                                "integer symbolic load stopped folding");
    passed &= expectContains(integerResult, "lw", "folded lw is missing");
    passed &= expectContains(integerResult, "global_int",
                             "folded lw lost its symbol");

    if (passed) std::cout << "Peephole floating-load regression passed\n";
    return passed ? 0 : 1;
}
