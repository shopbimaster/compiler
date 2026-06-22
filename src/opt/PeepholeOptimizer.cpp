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

// 精确匹配指令助记符：prefix 后必须紧跟空格/tab 或行尾
bool tryMatch(const std::string& line, const std::string& prefix,
              std::string& rd, std::string& rs, std::string& imm) {
    auto trimmed = line;
    size_t p = 0;
    while (p < trimmed.size() && (trimmed[p] == ' ' || trimmed[p] == '\t')) ++p;
    if (trimmed.substr(p, prefix.size()) != prefix) return false;
    p += prefix.size();
    // 确保 prefix 后面是空格/tab/行尾，防止 add 匹配 addw/addi/addiw
    if (p < trimmed.size() && trimmed[p] != ' ' && trimmed[p] != '\t') return false;
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

// 提取指令助记符（去除前导空格）
std::string extractOpName(const std::string& line) {
    auto start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    auto end = line.find_first_of(" \t", start);
    if (end == std::string::npos) return line.substr(start);
    return line.substr(start, end - start);
}

// 匹配 bnez/beqz 指令：bnez rs, label
bool tryMatchBranch(const std::string& line, const std::string& prefix,
                    std::string& rs, std::string& label) {
    auto trimmed = line;
    size_t p = 0;
    while (p < trimmed.size() && (trimmed[p] == ' ' || trimmed[p] == '\t')) ++p;
    if (trimmed.substr(p, prefix.size()) != prefix) return false;
    p += prefix.size();
    if (p < trimmed.size() && trimmed[p] != ' ' && trimmed[p] != '\t') return false;
    while (p < trimmed.size() && (trimmed[p] == ' ' || trimmed[p] == '\t')) ++p;
    if (p >= trimmed.size()) return false;
    rs = extractReg(trimmed, p);
    p += rs.size();
    while (p < trimmed.size() && (trimmed[p] == ' ' || trimmed[p] == '\t' || trimmed[p] == ',')) ++p;
    if (p >= trimmed.size()) return false;
    label = trimmed.substr(p);
    while (!label.empty() && (label.back() == ' ' || label.back() == '\t')) label.pop_back();
    return true;
}

std::string peepholeOptimize(const std::string& asmCode) {
    auto lines = splitLines(asmCode);

    // 迭代到收敛（最多 3 次），处理级联变换
    for (int iter = 0; iter < 3; ++iter) {
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

            // addiw rd, reg, 0 → sext.w rd, reg  (实际上就是 mv + 符号扩展，但 addiw 本身就可当做 sext.w)
            // 不过 addiw rd, rs, 0 本身是合法的，这里跳过，由上面的 addi 处理

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
                    // li rd, 0; addw rd, rd, rs → mv rd, rs
                    if (!matched && liImm == "0" && tryMatch(lines[i + 1], "addw", addRd, addRs1, addRs2)) {
                        if (liRd == addRd && addRs1 == liRd) {
                            result.push_back("  mv      " + liRd + ", " + addRs2);
                            ++i;
                            matched = true;
                        }
                    }
                    // li rd, 0; subw rd, rd, rs → negw rd, rs
                    if (!matched && liImm == "0" && tryMatch(lines[i + 1], "subw", addRd, addRs1, addRs2)) {
                        if (liRd == addRd && addRs1 == liRd) {
                            result.push_back("  negw    " + liRd + ", " + addRs2);
                            ++i;
                            matched = true;
                        }
                    }
                    // li rd, 0; sub rd, rd, rs → neg rd, rs
                    if (!matched && liImm == "0" && tryMatch(lines[i + 1], "sub", addRd, addRs1, addRs2)) {
                        if (liRd == addRd && addRs1 == liRd) {
                            result.push_back("  neg     " + liRd + ", " + addRs2);
                            ++i;
                            matched = true;
                        }
                    }
                    // li rd, 0; mv rd2, rd → li rd2, 0
                    if (!matched && liImm == "0") {
                        std::string mvRd, mvRs;
                        if (tryMatch(lines[i + 1], "mv", mvRd, mvRs, imm)) {
                            if (mvRs == liRd) {
                                result.push_back("  li      " + mvRd + ", 0");
                                ++i;
                                matched = true;
                            }
                        }
                    }
                }
            }

            // mv rd, rs; slli rd, rd, imm → slli rd, rs, imm
            // mv rd, rs; srli rd, rd, imm → srli rd, rs, imm
            // mv rd, rs; srai rd, rd, imm → srai rd, rs, imm
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm)) {
                    std::string sRd, sRs, sImm;
                    if ((tryMatch(lines[i + 1], "slli", sRd, sRs, sImm) ||
                         tryMatch(lines[i + 1], "srli", sRd, sRs, sImm) ||
                         tryMatch(lines[i + 1], "srai", sRd, sRs, sImm)) &&
                        sRd == mvRd && sRs == mvRd) {
                        std::string opName = extractOpName(lines[i + 1]);
                        result.push_back("  " + opName + "    " + sRd + ", " + mvRs + ", " + sImm);
                        ++i;
                        matched = true;
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

            // fsw rs, ...(sp) followed immediately by flw same, ...(sp) → fmv.s rd, rs
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string fsReg, fsOff;
                if (tryMatch(lines[i], "fsw", fsReg, fsOff, imm)) {
                    std::string flReg, flOff;
                    if (tryMatch(lines[i + 1], "flw", flReg, flOff, imm)) {
                        if (fsOff == flOff && !fsOff.empty()) {
                            result.push_back("  fmv.s   " + flReg + ", " + fsReg);
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // mv rd, rs followed by load/store using rd as address → use rs directly
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm)) {
                    std::string nextLine = lines[i + 1];
                    auto extractAddrReg = [](const std::string& line) -> std::string {
                        auto parenPos = line.find('(');
                        if (parenPos == std::string::npos) return "";
                        auto closePos = line.find(')', parenPos);
                        if (closePos == std::string::npos) return "";
                        return line.substr(parenPos + 1, closePos - parenPos - 1);
                    };
                    std::string addrReg = extractAddrReg(nextLine);
                    if (!addrReg.empty() && addrReg == mvRd) {
                        auto parenPos = nextLine.find('(' + addrReg + ')');
                        if (parenPos != std::string::npos) {
                            nextLine.replace(parenPos + 1, addrReg.size(), mvRs);
                        }
                        result.push_back(nextLine);
                        ++i;
                        matched = true;
                    }
                }
            }

            // mv rd, rs followed by mv rs, rd → mv rd, rs (the second mv is a no-op)
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mv1Rd, mv1Rs;
                if (tryMatch(lines[i], "mv", mv1Rd, mv1Rs, imm)) {
                    std::string mv2Rd, mv2Rs;
                    if (tryMatch(lines[i + 1], "mv", mv2Rd, mv2Rs, imm)) {
                        if (mv1Rd == mv2Rs && mv1Rs == mv2Rd) {
                            result.push_back(lines[i]);
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // Redundant mv reg, reg (self-assignment)
            if (!matched && tryMatch(lines[i], "mv", rd, rs, imm)) {
                if (rd == rs) {
                    matched = true;
                }
            }

            // addiw reg, reg, 0 → no-op (remove)
            if (!matched && tryMatch(lines[i], "addiw", rd, rs, imm)) {
                if (rd == rs && imm == "0") {
                    matched = true;
                }
            }

            // mv rd, rs; bnez rd, label → bnez rs, label
            // mv rd, rs; beqz rd, label → beqz rs, label
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm)) {
                    std::string brRs, brLabel;
                    if (tryMatchBranch(lines[i + 1], "bnez", brRs, brLabel) && brRs == mvRd) {
                        result.push_back("  bnez    " + mvRs + ", " + brLabel);
                        ++i;
                        matched = true;
                    } else if (tryMatchBranch(lines[i + 1], "beqz", brRs, brLabel) && brRs == mvRd) {
                        result.push_back("  beqz    " + mvRs + ", " + brLabel);
                        ++i;
                        matched = true;
                    }
                }
            }

            // addi rd, rs, 0; snez/seqz/sltz/sgtz rd, rd → snez/seqz/sltz/sgtz rd, rs
            // 这处理 addi→mv 转换后的级联场景
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string addiRd, addiRs, addiImm;
                if (tryMatch(lines[i], "addi", addiRd, addiRs, addiImm) && addiImm == "0") {
                    std::string szRd, szRs;
                    auto trySxz = [&](const std::string& name) -> bool {
                        return tryMatch(lines[i + 1], name, szRd, szRs, imm);
                    };
                    if ((trySxz("snez") || trySxz("seqz") || trySxz("sltz") || trySxz("sgtz")) &&
                        szRd == addiRd && szRs == addiRd) {
                        std::string opName = extractOpName(lines[i + 1]);
                        result.push_back("  " + opName + "    " + szRd + ", " + addiRs);
                        ++i;
                        matched = true;
                    }
                }
            }

            // mv rd, rs; snez/seqz/sltz/sgtz rd, rd → snez/seqz/sltz/sgtz rd, rs
            // 处理上一轮 addi→mv 转换后的级联
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm)) {
                    std::string szRd, szRs;
                    auto trySxz = [&](const std::string& name) -> bool {
                        return tryMatch(lines[i + 1], name, szRd, szRs, imm);
                    };
                    if ((trySxz("snez") || trySxz("seqz") || trySxz("sltz") || trySxz("sgtz")) &&
                        szRd == mvRd && szRs == mvRd) {
                        std::string opName = extractOpName(lines[i + 1]);
                        result.push_back("  " + opName + "    " + szRd + ", " + mvRs);
                        ++i;
                        matched = true;
                    }
                }
            }

            // <op> rd, rs, rs2/imm; mv rd2, rd → <op> rd2, rs, rs2/imm
            if (!matched) {
                size_t nextIdx = i + 1;
                while (nextIdx < lines.size() && isEmptyOrComment(lines[nextIdx])) {
                    nextIdx++;
                }
                if (nextIdx < lines.size()) {
                    std::string opRd, opRs, opRs2;
                    std::string mvRd, mvRs;
                    bool isThreeReg = false;
                    std::string opName;

                    auto tryThreeRegOp = [&](const std::string& name) -> bool {
                        return tryMatch(lines[i], name, opRd, opRs, opRs2) && !opRs2.empty();
                    };

                    // 三寄存器操作
                    if (tryThreeRegOp("addw") || tryThreeRegOp("subw") || tryThreeRegOp("sllw") ||
                        tryThreeRegOp("sraw") || tryThreeRegOp("mulw") || tryThreeRegOp("divw") ||
                        tryThreeRegOp("remw") || tryThreeRegOp("add")  || tryThreeRegOp("sub")  ||
                        tryThreeRegOp("sll")  || tryThreeRegOp("sra")  || tryThreeRegOp("or")   ||
                        tryThreeRegOp("and")  || tryThreeRegOp("xor")  || tryThreeRegOp("slt")  ||
                        tryThreeRegOp("sltu")) {
                        isThreeReg = true;
                        opName = extractOpName(lines[i]);
                    }

                    if (isThreeReg && tryMatch(lines[nextIdx], "mv", mvRd, mvRs, imm)) {
                        if (mvRs == opRd) {
                            // 检查 opRd 在 mv 之后是否还被后续指令使用
                            // 如果被使用，不能合并，因为合并后 opRd 不再被写入
                            bool opRdUsedAfter = false;
                            for (size_t k = nextIdx + 1; k < lines.size(); ++k) {
                                if (isEmptyOrComment(lines[k])) continue;
                                // 检查 opRd 是否作为寄存器操作数出现
                                // 匹配 opRd 前后有空格、逗号、括号或行尾
                                const std::string& l = lines[k];
                                size_t pos = 0;
                                while ((pos = l.find(opRd, pos)) != std::string::npos) {
                                    char before = (pos > 0) ? l[pos - 1] : ' ';
                                    char after = (pos + opRd.size() < l.size()) ? l[pos + opRd.size()] : ' ';
                                    bool validBefore = (before == ' ' || before == '\t' || before == ',' || before == '(');
                                    bool validAfter = (after == ' ' || after == '\t' || after == ',' || after == ')');
                                    if (validBefore && validAfter) {
                                        opRdUsedAfter = true;
                                        break;
                                    }
                                    pos += opRd.size();
                                }
                                if (opRdUsedAfter) break;
                            }
                            if (!opRdUsedAfter) {
                                result.push_back("  " + opName + "    " + mvRd + ", " + opRs + ", " + opRs2);
                                for (size_t j = i + 1; j < nextIdx; ++j) {
                                    result.push_back(lines[j]);
                                }
                                i = nextIdx;
                                matched = true;
                            }
                        }
                    }

                    // 两寄存器+立即数操作
                    if (!matched) {
                        auto tryImmOp = [&](const std::string& name) -> bool {
                            return tryMatch(lines[i], name, opRd, opRs, opRs2) && !opRs2.empty();
                        };
                        if (tryImmOp("addiw") || tryImmOp("addi") ||
                            tryImmOp("slli") || tryImmOp("srli") || tryImmOp("srai")) {
                            opName = extractOpName(lines[i]);
                            if (tryMatch(lines[nextIdx], "mv", mvRd, mvRs, imm)) {
                                if (mvRs == opRd) {
                                    // 检查 opRd 在 mv 之后是否还被后续指令使用
                                    bool opRdUsedAfter = false;
                                    for (size_t k = nextIdx + 1; k < lines.size(); ++k) {
                                        if (isEmptyOrComment(lines[k])) continue;
                                        const std::string& l = lines[k];
                                        size_t pos = 0;
                                        while ((pos = l.find(opRd, pos)) != std::string::npos) {
                                            char before = (pos > 0) ? l[pos - 1] : ' ';
                                            char after = (pos + opRd.size() < l.size()) ? l[pos + opRd.size()] : ' ';
                                            bool validBefore = (before == ' ' || before == '\t' || before == ',' || before == '(');
                                            bool validAfter = (after == ' ' || after == '\t' || after == ',' || after == ')');
                                            if (validBefore && validAfter) {
                                                opRdUsedAfter = true;
                                                break;
                                            }
                                            pos += opRd.size();
                                        }
                                        if (opRdUsedAfter) break;
                                    }
                                    if (!opRdUsedAfter) {
                                        result.push_back("  " + opName + "    " + mvRd + ", " + opRs + ", " + opRs2);
                                        for (size_t j = i + 1; j < nextIdx; ++j) {
                                            result.push_back(lines[j]);
                                        }
                                        i = nextIdx;
                                        matched = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // li rN, N; op rd, rs, rN → opi rd, rs, N
            // 支持: addw/add/subw/sub/and/or/xor/slt/sltu/sllw/srlw/sraw
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string liRd, liImm;
                if (tryMatch(lines[i], "li", liRd, liImm, imm)) {
                    int64_t liVal = std::stoll(liImm);
                    bool fitI12 = (-2048 <= liVal && liVal <= 2047);
                    std::string opRd, opRs, opRs2;

                    auto tryLiOp = [&](const std::string& op, const std::string& opi, int64_t val, bool cond) -> bool {
                        if (!cond) return false;
                        if (!tryMatch(lines[i + 1], op, opRd, opRs, opRs2)) return false;
                        if (opRs2 != liRd) return false;
                        // 检查 op 后面是否有 mv 可以合并
                        size_t mvIdx = i + 2;
                        while (mvIdx < lines.size() && isEmptyOrComment(lines[mvIdx])) {
                            mvIdx++;
                        }
                        std::string mvRd, mvRs;
                        if (mvIdx < lines.size() && tryMatch(lines[mvIdx], "mv", mvRd, mvRs, imm)) {
                            if (mvRs == opRd) {
                                // 检查 opRd 在 mv 之后是否还被后续指令使用
                                bool opRdUsedAfter = false;
                                for (size_t k = mvIdx + 1; k < lines.size(); ++k) {
                                    if (isEmptyOrComment(lines[k])) continue;
                                    const std::string& l = lines[k];
                                    size_t pos = 0;
                                    while ((pos = l.find(opRd, pos)) != std::string::npos) {
                                        char before = (pos > 0) ? l[pos - 1] : ' ';
                                        char after = (pos + opRd.size() < l.size()) ? l[pos + opRd.size()] : ' ';
                                        bool validBefore = (before == ' ' || before == '\t' || before == ',' || before == '(');
                                        bool validAfter = (after == ' ' || after == '\t' || after == ',' || after == ')');
                                        if (validBefore && validAfter) {
                                            opRdUsedAfter = true;
                                            break;
                                        }
                                        pos += opRd.size();
                                    }
                                    if (opRdUsedAfter) break;
                                }
                                if (!opRdUsedAfter) {
                                    result.push_back("  " + opi + "    " + mvRd + ", " + opRs + ", " + std::to_string(val));
                                    for (size_t j = i + 2; j < mvIdx; ++j) {
                                        result.push_back(lines[j]);
                                    }
                                    i = mvIdx;
                                    return true;
                                }
                            }
                        }
                        // 无 mv 合并
                        result.push_back("  " + opi + "    " + opRd + ", " + opRs + ", " + std::to_string(val));
                        ++i;
                        return true;
                    };

                    // addw → addiw (N 在 12-bit 范围内)
                    if (tryLiOp("addw", "addiw", liVal, fitI12)) { matched = true; }
                    // add → addi
                    else if (tryLiOp("add", "addi", liVal, fitI12)) { matched = true; }
                    // subw → addiw rd, rs, -N
                    else if (tryLiOp("subw", "addiw", -liVal, fitI12 && (-2048 <= -liVal && -liVal <= 2047))) { matched = true; }
                    // sub → addi rd, rs, -N
                    else if (tryLiOp("sub", "addi", -liVal, fitI12 && (-2048 <= -liVal && -liVal <= 2047))) { matched = true; }
                    // and → andi
                    else if (tryLiOp("and", "andi", liVal, fitI12)) { matched = true; }
                    // or → ori
                    else if (tryLiOp("or", "ori", liVal, fitI12)) { matched = true; }
                    // xor → xori
                    else if (tryLiOp("xor", "xori", liVal, fitI12)) { matched = true; }
                    // slt → slti
                    else if (tryLiOp("slt", "slti", liVal, fitI12)) { matched = true; }
                    // sltu → sltiu
                    else if (tryLiOp("sltu", "sltiu", liVal, fitI12)) { matched = true; }
                    // sllw → slliw (N 在 0-31 范围内)
                    else if (tryLiOp("sllw", "slliw", liVal, 0 <= liVal && liVal <= 31)) { matched = true; }
                    // srlw → srliw
                    else if (tryLiOp("srlw", "srliw", liVal, 0 <= liVal && liVal <= 31)) { matched = true; }
                    // sraw → sraiw
                    else if (tryLiOp("sraw", "sraiw", liVal, 0 <= liVal && liVal <= 31)) { matched = true; }
                }
            }

            // mv reg, src followed by sw/fsw/sd reg, offset(sp) → sw/fsw/sd src, offset(sp)
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm)) {
                    std::string swReg, swOff;
                    if (tryMatch(lines[i + 1], "sw", swReg, swOff, imm)) {
                        if (swReg == mvRd) {
                            result.push_back("  sw      " + mvRs + ", " + swOff);
                            ++i;
                            matched = true;
                        }
                    }
                    if (!matched && tryMatch(lines[i + 1], "fsw", swReg, swOff, imm)) {
                        if (swReg == mvRd) {
                            result.push_back("  fsw     " + mvRs + ", " + swOff);
                            ++i;
                            matched = true;
                        }
                    }
                    if (!matched && tryMatch(lines[i + 1], "sd", swReg, swOff, imm)) {
                        if (swReg == mvRd) {
                            result.push_back("  sd      " + mvRs + ", " + swOff);
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

        // 检查是否收敛
        if (result.size() == lines.size()) {
            bool same = true;
            for (size_t j = 0; j < lines.size(); ++j) {
                if (result[j] != lines[j]) { same = false; break; }
            }
            if (same) break;
        }
        lines = std::move(result);
    }

    return joinLines(lines);
}

} // namespace Opt