#pragma once

#include <string>
#include <vector>
#include <regex>

namespace Backend {

class PeepholeOptimizer {
private:
    struct Pattern {
        std::regex pattern;
        std::string replacement;
        bool active;
    };
    std::vector<Pattern> patterns;

public:
    PeepholeOptimizer();

    std::string optimize(const std::string& asmCode);

private:
    void initPatterns();
    std::string optimizeSequence(const std::vector<std::string>& lines);
};

}
