// ================================================================
// O1: 汇编级窥孔优化 —— 逐行扫描消除冗余指令组合
// ================================================================

#include "opt/Optimizer.h"
#include <regex>
#include <sstream>
#include <vector>

namespace Opt {
namespace {

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string joinLines(const std::vector<std::string>& lines) {
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) result += "\n";
        result += lines[i];
    }
    return result;
}

bool isEmptyOrComment(const std::string& line) {
    auto trimmed = line;
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
        trimmed = trimmed.substr(1);
    return trimmed.empty() || trimmed[0] == '#' || trimmed[0] == '.';
}

std::string extractReg(const std::string& s, size_t pos) {
    size_t end = pos;
    while (end < s.size() && s[end] != ' ' && s[end] != '\t' && s[end] != ',')
        ++end;
    return s.substr(pos, end - pos);
}

bool tryMatch(const std::string& line, const std::string& prefix,
              std::string& rd, std::string& rs, std::string& imm) {
    auto trimmed = line;
    size_t p = 0;
    while (p < trimmed.size() && (trimmed[p] == ' ' || trimmed[p] == '\t')) ++p;
    if (trimmed.substr(p, prefix.size()) != prefix) return false;
    p += prefix.size();
    while (p < trimmed.size() && (trimmed[p] == ' ' || trimmed[p] == '\t')) ++p;
    if (p >= trimmed.size()) return false;
    rd = extractReg(trimmed, p);
    p += rd.size();
    while (p < trimmed.size() && (trimmed[p] == ' ' || trimmed[p] == '\t' || trimmed[p] == ',')) ++p;
    if (p >= trimmed.size()) { rs = ""; imm = ""; return true; }
    rs = extractReg(trimmed, p);
    p += rs.size();
    while (p < trimmed.size() && (trimmed[p] == ' ' || trimmed[p] == '\t' || trimmed[p] == ',')) ++p;
    if (p >= trimmed.size()) { imm = ""; return true; }
    imm = trimmed.substr(p);
    while (!imm.empty() && (imm.back() == ' ' || imm.back() == '\t')) imm.pop_back();
    return true;
}

} // namespace

std::string peepholeOptimize(const std::string& asmCode) {
    auto lines = splitLines(asmCode);
    std::vector<std::string> result;
    result.reserve(lines.size());

    for (size_t i = 0; i < lines.size(); ++i) {
        if (isEmptyOrComment(lines[i])) {
            result.push_back(lines[i]);
            continue;
        }

        std::string rd, rs, imm;
        bool matched = false;

        // addi rd, rs, 0 → mv rd, rs
        if (tryMatch(lines[i], "addi", rd, rs, imm)) {
            if (imm == "0" && !rd.empty() && !rs.empty()) {
                result.push_back("  mv      " + rd + ", " + rs);
                matched = true;
            }
        }

        // li rd, 0 followed by add rd, rd, rs → mv rd, rs
        if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
            std::string liRd, liImm;
            if (tryMatch(lines[i], "li", liRd, liImm, imm)) {
                std::string addRd, addRs1, addRs2;
                if (liImm == "0" && tryMatch(lines[i + 1], "add", addRd, addRs1, addRs2)) {
                    if (liRd == addRd && addRs1 == liRd) {
                        result.push_back("  mv      " + liRd + ", " + addRs2);
                        ++i;
                        matched = true;
                    }
                }
            }
        }

        // sw rs, ...(sp) followed immediately by lw same, ...(sp) → mv rd, rs
        if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
            std::string swReg, swOff;
            if (tryMatch(lines[i], "sw", swReg, swOff, imm)) {
                std::string lwReg, lwOff;
                if (tryMatch(lines[i + 1], "lw", lwReg, lwOff, imm)) {
                    if (swOff == lwOff && !swOff.empty()) {
                        result.push_back("  mv      " + lwReg + ", " + swReg);
                        ++i;
                        matched = true;
                    }
                }
            }
        }

        // Redundant mv reg, reg (self-assignment)
        if (!matched && tryMatch(lines[i], "mv", rd, rs, imm)) {
            if (rd == rs) {
                ++i;
                matched = true;
            }
        }

        // mv reg, src followed by sw reg, offset(sp) → sw src, offset(sp)
        if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
            std::string mvRd, mvRs;
            if (tryMatch(lines[i], "mv", mvRd, mvRs, imm)) {
                std::string swReg, swOff;
                if (tryMatch(lines[i + 1], "sw", swReg, swOff, imm)) {
                    if (swReg == mvRd) {
                        // Replace: sw mvRd, offset(sp) → sw mvRs, offset(sp)
                        result.push_back("  sw      " + mvRs + ", " + swOff);
                        ++i;
                        matched = true;
                    }
                }
            }
        }

        if (!matched) {
            result.push_back(lines[i]);
        }
    }

    return joinLines(result);
}

} // namespace Opt