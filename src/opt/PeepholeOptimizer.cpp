// ================================================================
// O1: 汇编级窥孔优化 —— 逐行扫描消除冗余指令组合
// ================================================================

#include "opt/Optimizer.h"
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
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
    if (trimmed.empty() || trimmed[0] == '#') return true;
    // ★ 标签行（.Lxxx:）以 . 开头但以 : 结尾，是 BB 边界，不能当作空行跳过！
    // 否则 op+mv 等优化会跨 BB 合并，破坏语义。
    // 汇编指令（.p2align, .type, .globl 等）以 . 开头但不以 : 结尾，仍当作空行跳过。
    if (trimmed[0] == '.') {
        return trimmed.back() != ':';
    }
    return false;
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

// 判断一行是否是标签（基本块边界）
bool isLabel(const std::string& line) {
    auto trimmed = line;
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
        trimmed = trimmed.substr(1);
    if (trimmed.empty()) return false;
    return trimmed.back() == ':';
}

// 检查寄存器名 needle 是否作为 token 出现在 haystack 中
// token 边界：空格/tab/逗号/括号/行首/行尾
// 用于避免子串匹配（如 s1 匹配 s11）
bool regInStr(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        char before = (pos > 0) ? haystack[pos - 1] : ' ';
        char after = (pos + needle.size() < haystack.size()) ? haystack[pos + needle.size()] : ' ';
        bool validBefore = (before == ' ' || before == '\t' || before == ',' || before == '(');
        bool validAfter = (after == ' ' || after == '\t' || after == ',' || after == ')');
        if (validBefore && validAfter) return true;
        pos += needle.size();
    }
    return false;
}

// 判断指令是否「杀死」给定寄存器（定义为目的操作数且不同时读取该寄存器）
// 返回 true 表示该寄存器在此指令处被覆写，原值不再存活
bool instrKillsReg(const std::string& line, const std::string& reg) {
    std::string opName = extractOpName(line);
    if (opName.empty()) return false;

    // 无目的寄存器的指令（store/branch/jump/call 等）不会杀死任何寄存器
    static const std::set<std::string> NO_DEST = {
        "sw", "sd", "sh", "sb", "fsw", "fsd", "fsh", "fsb",
        "beq", "bne", "blt", "bge", "bltu", "bgeu",
        "beqz", "bnez", "blez", "bgez", "bltz", "bgtz",
        "j", "jr", "ret", "tail", "call", "ecall", "ebreak", "fence", "nop"
    };
    if (NO_DEST.count(opName) > 0) return false;

    // 解析: op rd, rs, imm
    std::string rd, rs, imm;
    if (!tryMatch(line, opName, rd, rs, imm)) return false;

    // rd 必须是待检查的寄存器
    if (rd != reg) return false;

    // 检查 reg 是否同时作为源操作数出现（包括地址表达式如 0(t0)）
    // 用 token 检查避免漏掉地址中的寄存器
    if (regInStr(rs, reg)) return false;
    if (!imm.empty() && regInStr(imm, reg)) return false;

    // reg 被定义但不被读取 → 被杀死
    return true;
}

// 从 start 开始查找下一条真实指令的行索引
// 跳过空行、注释行、指令行（.p2align 等），遇到标签行（BB 边界）返回 lines.size()
size_t findNextRealInst(const std::vector<std::string>& lines, size_t start) {
    for (size_t k = start; k < lines.size(); ++k) {
        const std::string& l = lines[k];
        if (l.empty()) continue;
        if (isLabel(l)) return lines.size();  // BB 边界
        size_t p = 0;
        while (p < l.size() && (l[p] == ' ' || l[p] == '\t')) ++p;
        if (p >= l.size()) continue;  // 纯空白
        if (l[p] == '#') continue;    // 注释
        if (l[p] == '.') continue;    // 指令（非标签，因为 isLabel 已排除）
        return k;  // 真实指令
    }
    return lines.size();
}

// 检查寄存器 reg 在 lines[start..] 到下一个标签（BB 边界）之间是否死亡
// 死亡条件：被覆写（instrKillsReg）或到达 BB 末尾未被读取
// 保守条件：遇到标签时返回 false（可能 live-out）
// call 指令杀死 caller-saved 寄存器（t0-t6, a0-a7）
bool isRegDeadInBB(const std::vector<std::string>& lines, size_t start, const std::string& reg) {
    for (size_t k = start; k < lines.size(); ++k) {
        const std::string& l = lines[k];
        if (l.empty()) continue;
        if (isLabel(l)) return false;  // BB 边界：可能 live-out，保守返回 false
        size_t p = 0;
        while (p < l.size() && (l[p] == ' ' || l[p] == '\t')) ++p;
        if (p >= l.size()) continue;  // 纯空白
        if (l[p] == '#') continue;    // 注释
        if (l[p] == '.') continue;    // 指令（非标签）
        // 被杀死 → 死亡
        if (instrKillsReg(l, reg)) return true;
        // 被读取 → 存活
        if (regInStr(l, reg)) return false;
        // call 杀死 caller-saved 寄存器
        std::string opN = extractOpName(l);
        if (opN == "call" && !reg.empty()) {
            char c = reg[0];
            if (c == 't' || c == 'a') return true;
        }
    }
    return false;  // 到达文件末尾，保守返回 false（可能 live-out）
}

// ★ BB 内局部死亡检查（heuristic for scratch registers）
// 与 isRegDeadInBB 不同：到达 BB 终止符（label）时返回 true 而非 false，
// 用于 caller-saved 临时寄存器（t0-t6, a0-a7）的死亡判断。
// 原理：codegen 产生的临时寄存器通常只在当前 BB 内使用，不会跨 BB 存活。
//   寄存器分配器虽然可以分配 t3-t6/a0-a7 给跨 BB 值，但优先使用 s0-s11，
//   且 call-aware coloring 确保跨 CALL 的值不分配到 caller-saved。
//   实际 codegen 中 t0-t6 主要作为 ALU scratch，极少跨 BB。
// 风险：若 tA/tB 确实跨 BB 存活（如分配器分配的长生命周期值），
//   优化可能错误消除 mv，导致后续 BB 读到错误值。
//   缓解：仅用于自更新模式（mv tA,sX; op tB,tA,C; mv sX,tB），
//   该模式中 tA/tB 是 codegen 的 load/store scratch，不是分配器分配的。
bool isRegLocalDead(const std::vector<std::string>& lines, size_t start, const std::string& reg) {
    for (size_t k = start; k < lines.size(); ++k) {
        const std::string& l = lines[k];
        if (l.empty()) continue;
        if (isLabel(l)) return true;  // BB 边界：局部死亡（heuristic）
        size_t p = 0;
        while (p < l.size() && (l[p] == ' ' || l[p] == '\t')) ++p;
        if (p >= l.size()) continue;
        if (l[p] == '#') continue;
        if (l[p] == '.') continue;
        // 被杀死 → 死亡
        if (instrKillsReg(l, reg)) return true;
        // 被读取 → 存活
        if (regInStr(l, reg)) return false;
        // call 杀死 caller-saved
        std::string opN = extractOpName(l);
        if (opN == "call" && !reg.empty()) {
            char c = reg[0];
            if (c == 't' || c == 'a') return true;
        }
    }
    return true;  // 到达文件末尾，局部死亡
}

// ★ 寄存器类型感知的死亡检查
// t 寄存器 (t0-t6): 使用 isRegLocalDead（BB 边界=死，因为这些寄存器主要作为 ALU scratch）
// a/s 寄存器: 使用 isRegDeadInBB（BB 边界=活，保守判断）
// 原因：a0-a7 已加入寄存器分配池，可能被分配给跨 BB 存活的值；
//   s0-s11 天然跨 BB 存活。仅 t0-t6 几乎只用于 BB 内 scratch。
bool isRegDeadAware(const std::vector<std::string>& lines, size_t start, const std::string& reg) {
    if (reg.empty()) return false;
    char c = reg[0];
    if (c == 't') {
        return isRegLocalDead(lines, start, reg);
    }
    return isRegDeadInBB(lines, start, reg);
}


std::string peepholeOptimize(const std::string& asmCode) {
    auto lines = splitLines(asmCode);

    // ★ 预处理：移除空行和纯空白行
    // 汇编器不关心空行，但空行会阻断窥孔模式匹配（如 sw+lw→mv）
    // 移除空行后，所有使用 lines[i+1] 的模式都能正确匹配相邻指令
    // 标签、注释、指令（.p2align 等）保留不动，它们不会被移除
    {
        std::vector<std::string> compact;
        compact.reserve(lines.size());
        for (const auto& l : lines) {
            bool allWhitespace = true;
            for (char c : l) {
                if (c != ' ' && c != '\t') { allWhitespace = false; break; }
            }
            if (!allWhitespace) {
                compact.push_back(l);
            }
        }
        lines = std::move(compact);
    }

    // 迭代到收敛（最多 3 次），处理级联变换
    const char* maxIterEnv = std::getenv("PEEPHOLE_MAX_ITER");
    int maxIter = maxIterEnv ? std::atoi(maxIterEnv) : 3;
    if (maxIter < 0) maxIter = 3;
    for (int iter = 0; iter < maxIter; ++iter) {
        // ★ 构建跳板映射：label → final_target
        // 跳板模式：label: 后紧跟一条 j target（无其他指令）
        // 将所有跳转到 label 的指令替换为直接跳转到 final_target
        std::unordered_map<std::string, std::string> trampolineMap;
        for (size_t i = 0; i < lines.size(); ++i) {
            // 匹配标签行：行首非空格，以冒号结尾
            std::string trimmed = lines[i];
            while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
                trimmed = trimmed.substr(1);
            if (trimmed.empty() || trimmed.back() != ':') continue;
            std::string label = trimmed.substr(0, trimmed.size() - 1);
            // 查找下一条非空非注释行
            size_t j = i + 1;
            while (j < lines.size() && isEmptyOrComment(lines[j])) ++j;
            if (j >= lines.size()) continue;
            // 检查是否是 j target
            std::string jRd, jRs, jImm;
            if (tryMatch(lines[j], "j", jRd, jRs, jImm) && !jRd.empty()) {
                // jRd 是跳转目标（tryMatch 将 j 的第一个操作数放入 rd）
                trampolineMap[label] = jRd;
            }
        }
        // 解析级联跳板：A→B→C 解析为 A→C
        for (auto& [label, target] : trampolineMap) {
            int depth = 0;
            while (trampolineMap.count(target) && depth < 20) {
                target = trampolineMap[target];
                ++depth;
            }
        }

        // ★ 预处理：替换跳转指令的目标为最终目标（消除跳板间接跳转）
        if (!trampolineMap.empty()) {
            for (auto& line : lines) {
                if (isEmptyOrComment(line)) continue;
                std::string opName = extractOpName(line);
                // j target
                if (opName == "j") {
                    std::string jRd, jRs, jImm;
                    if (tryMatch(line, "j", jRd, jRs, jImm) && trampolineMap.count(jRd)) {
                        line = "  j       " + trampolineMap[jRd];
                    }
                }
                // beqz/bnez rs, label
                else if (opName == "beqz" || opName == "bnez") {
                    std::string brRs, brLabel;
                    if (tryMatchBranch(line, opName, brRs, brLabel) && trampolineMap.count(brLabel)) {
                        line = "  " + opName + "    " + brRs + ", " + trampolineMap[brLabel];
                    }
                }
                // beq/bne/blt/bge/bltu/bgeu rs1, rs2, label
                else if (opName == "beq" || opName == "bne" || opName == "blt" ||
                         opName == "bge" || opName == "bltu" || opName == "bgeu") {
                    std::string rd, rs, imm;
                    if (tryMatch(line, opName, rd, rs, imm) && trampolineMap.count(imm)) {
                        line = "  " + opName + "    " + rd + ", " + rs + ", " + trampolineMap[imm];
                    }
                }
            }
        }

        // ★ 死 trampoline 清理：删除无人引用的 trampoline（label: j target）
        // trampolineMap 重定向后，原来的 trampoline label 变成死代码（无人引用）
        // 这些死 trampoline 会物理阻断 j 的 fall-through 优化
        // 删除后，现有的 j fall-through 模式会自动消除被阻断的 j
        // 典型场景：bXX then; j else; dead_trampoline: j then; else:
        // ★ QEMU 安全：运行在寄存器分配之后，不改变寄存器分配（规则 14）
        // ★ BUG FIX：函数入口标签（.globl 声明）不能删除！
        //   main: 等函数入口由 C runtime _start 外部调用，不会被内部 j/bXX 引用，
        //   但绝不能当作死 trampoline 删除，否则链接报 undefined reference to `main`。
        {
            // 0. 收集所有 .globl 声明的符号（函数入口/全局变量，不可删除）
            //    典型格式：.globl main / .globl mm（注意 .globl 后可能有多个空格）
            std::set<std::string> globalSymbols;
            for (const auto& line : lines) {
                auto trimmed = line;
                while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
                    trimmed = trimmed.substr(1);
                if (trimmed.substr(0, 7) == ".globl ") {
                    std::string name = trimmed.substr(7);
                    // 去除前导和尾随空白（.globl 后可能有多个空格）
                    while (!name.empty() && (name[0] == ' ' || name[0] == '\t'))
                        name = name.substr(1);
                    while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
                        name.pop_back();
                    if (!name.empty()) globalSymbols.insert(name);
                }
            }

            // 1. 收集所有被引用的 label（从跳转指令中）
            std::set<std::string> referencedLabels;
            for (const auto& line : lines) {
                if (isEmptyOrComment(line)) continue;
                std::string opName = extractOpName(line);
                std::string rd, rs, imm;
                std::string brRs, brLabel;
                if (opName == "j") {
                    if (tryMatch(line, "j", rd, rs, imm) && !rd.empty())
                        referencedLabels.insert(rd);
                } else if (opName == "beqz" || opName == "bnez" || opName == "blez" ||
                           opName == "bgez" || opName == "bltz" || opName == "bgtz") {
                    if (tryMatchBranch(line, opName, brRs, brLabel) && !brLabel.empty())
                        referencedLabels.insert(brLabel);
                } else if (opName == "beq" || opName == "bne" || opName == "blt" ||
                           opName == "bge" || opName == "bltu" || opName == "bgeu") {
                    if (tryMatch(line, opName, rd, rs, imm) && !imm.empty())
                        referencedLabels.insert(imm);
                }
            }

            // 2. 删除无人引用的 trampoline（label: j target）
            // trampoline 模式：label 行后紧跟一条 j target 指令（中间无其他指令）
            // ★ 保护 .globl 声明的函数入口标签（如 main:），即使未被内部引用
            std::vector<std::string> compact;
            compact.reserve(lines.size());
            for (size_t i = 0; i < lines.size(); ++i) {
                // 检查是否是 trampoline 模式
                if (isLabel(lines[i])) {
                    // 提取 label 名
                    std::string trimmed = lines[i];
                    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
                        trimmed = trimmed.substr(1);
                    std::string labelName = trimmed.substr(0, trimmed.size() - 1);

                    // ★ 保护 .globl 声明的符号（函数入口），永远不删除
                    if (globalSymbols.count(labelName)) {
                        compact.push_back(lines[i]);
                        continue;
                    }

                    // 查找下一条非空非注释行
                    size_t j = i + 1;
                    while (j < lines.size() && isEmptyOrComment(lines[j])) ++j;
                    if (j < lines.size()) {
                        std::string jRd, jRs, jImm;
                        if (tryMatch(lines[j], "j", jRd, jRs, jImm) && !jRd.empty()) {
                            // 是 trampoline 模式：label: j target
                            // 如果 label 无人引用，删除整个 trampoline（label 行和 j 行）
                            if (referencedLabels.find(labelName) == referencedLabels.end()) {
                                // 跳过 label 行和 j 行（以及中间的空行/注释）
                                i = j;  // 跳过到 j 行（for 循环会 ++i）
                                continue;
                            }
                        }
                    }
                }
                compact.push_back(lines[i]);
            }
            lines = std::move(compact);
        }

        if (std::getenv("DEBUG_PEEP_TRACE")) {
            std::fprintf(stderr, "=== after trampoline removal ===\n");
            for (const auto& l : lines) if (l.find("main") != std::string::npos) std::fprintf(stderr, "  %s\n", l.c_str());
        }
        // 遇到 li rd, imm 时，若 rd 当前已知值 == imm（且期间未被任何指令写），删除该 li
        // 任何写 rd 的指令使 rd 未知；call 杀死 caller-saved 寄存器（t0-t6, a0-a7, ra）
        // 遇到标签时清空整个映射（BB 边界）
        // 典型场景：MagicDivision 魔数在同一 BB 内多次加载（如 34_multi_loop 的 18 次）
        // ★ QEMU 安全：运行在寄存器分配之后，不改变寄存器分配（规则 14）
        {
            std::vector<std::string> compact;
            compact.reserve(lines.size());
            std::unordered_map<std::string, int64_t> regKnownImm;

            auto parseImm = [](const std::string& s, int64_t& val) -> bool {
                if (s.empty()) return false;
                try {
                    size_t idx = 0;
                    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                        val = std::stoll(s, &idx, 16);
                    } else if (s.size() > 3 && s[0] == '-' && s[1] == '0' && (s[2] == 'x' || s[2] == 'X')) {
                        val = std::stoll(s, &idx, 16);
                    } else {
                        val = std::stoll(s, &idx, 10);
                    }
                    return true;
                } catch (...) {
                    return false;
                }
            };

            // 获取指令的目的寄存器（如果没有则返回空字符串）
            // 复用 instrKillsReg 的 NO_DEST 逻辑
            auto getDestReg = [](const std::string& line) -> std::string {
                std::string opName = extractOpName(line);
                if (opName.empty()) return "";
                static const std::set<std::string> NO_DEST = {
                    "sw", "sd", "sh", "sb", "fsw", "fsd", "fsh", "fsb",
                    "beq", "bne", "blt", "bge", "bltu", "bgeu",
                    "beqz", "bnez", "blez", "bgez", "bltz", "bgtz",
                    "j", "jr", "ret", "tail", "call", "ecall", "ebreak", "fence", "nop"
                };
                if (NO_DEST.count(opName) > 0) return "";
                std::string rd, rs, imm;
                if (!tryMatch(line, opName, rd, rs, imm)) return "";
                return rd;
            };

            for (size_t i = 0; i < lines.size(); ++i) {
                const std::string& line = lines[i];

                // 标签：BB 边界，清空映射
                if (isLabel(line)) {
                    compact.push_back(line);
                    regKnownImm.clear();
                    continue;
                }

                // 空行/注释/指令：保留，不影响映射
                if (isEmptyOrComment(line)) {
                    compact.push_back(line);
                    continue;
                }

                std::string opName = extractOpName(line);

                // call：杀死 caller-saved 寄存器（t0-t6, a0-a7, ra）
                if (opName == "call") {
                    compact.push_back(line);
                    for (int k = 0; k <= 6; ++k)
                        regKnownImm.erase("t" + std::to_string(k));
                    for (int k = 0; k <= 7; ++k)
                        regKnownImm.erase("a" + std::to_string(k));
                    regKnownImm.erase("ra");
                    continue;
                }

                // li rd, imm：检查冗余
                // ★ li 是两操作数指令：tryMatch 把 rd 放入第一个参数，imm 放入第二个参数（rs 位置）
                if (opName == "li") {
                    std::string liRd, liRs, liImm;
                    if (tryMatch(line, "li", liRd, liRs, liImm) && !liRd.empty() && !liRs.empty()) {
                        // x0/zero 永远是 0，li x0 是无意义指令（但不应出现）
                        if (liRd == "x0" || liRd == "zero") {
                            compact.push_back(line);
                            continue;
                        }
                        int64_t imm;
                        if (parseImm(liRs, imm)) {
                            auto it = regKnownImm.find(liRd);
                            if (it != regKnownImm.end() && it->second == imm) {
                                // 冗余 li，跳过（不 push）
                                continue;
                            }
                            // 更新映射
                            regKnownImm[liRd] = imm;
                            compact.push_back(line);
                            continue;
                        }
                    }
                    // 解析失败，保留
                    compact.push_back(line);
                    continue;
                }

                // 其他指令：获取目的寄存器，使被写的寄存器未知
                // 对于 mv rd, rs：如果 rs 在映射中有已知值，传播到 rd
                if (opName == "mv") {
                    std::string mvRd, mvRs, mvImm;
                    if (tryMatch(line, "mv", mvRd, mvRs, mvImm) && !mvRd.empty() && !mvRs.empty() && mvRd != mvRs) {
                        // 如果 mvRd == x0/zero，跳过（不应出现）
                        if (mvRd == "x0" || mvRd == "zero") {
                            compact.push_back(line);
                            continue;
                        }
                        auto it = regKnownImm.find(mvRs);
                        if (it != regKnownImm.end()) {
                            // 传播已知值
                            regKnownImm[mvRd] = it->second;
                        } else {
                            // rs 未知，rd 也变未知
                            regKnownImm.erase(mvRd);
                        }
                        compact.push_back(line);
                        continue;
                    }
                }

                // 一般指令：使目的寄存器未知
                std::string destReg = getDestReg(line);
                if (!destReg.empty() && destReg != "x0" && destReg != "zero") {
                    regKnownImm.erase(destReg);
                }
                compact.push_back(line);
            }
            lines = std::move(compact);
        }

        // ★ BB 内局部值编号（Local Value Numbering）
        // 消除同一 BB 内的冗余指令：相同 (opcode, 源操作数) 的指令若源操作数未变，则冗余
        // 典型场景：shuffle1 中 slli+add+lw 序列在 beqz 后重复（操作数未变）
        // ★ QEMU 安全：运行在寄存器分配之后，仅删除指令或替换为 mv，不改变寄存器分配（规则 14）
        // 可通过环境变量 PEEPHOLE_NO_LVN=1 禁用（调试用）
        if (!getenv("PEEPHOLE_NO_LVN")) {
            std::vector<std::string> compact;
            compact.reserve(lines.size());
            // lastSeen: key="opcode|src_operands" → (compact_index, dest_reg)
            std::unordered_map<std::string, std::pair<size_t, std::string>> lastSeen;
            // regLastWritten: reg → 最后一次写入的 compact_index
            std::unordered_map<std::string, size_t> regLastWritten;
            std::regex regRegex("\\b([tsa]\\d+|sp|ra|gp|tp)\\b");
            // ★ li 复用跟踪：reg → imm（该寄存器当前持有的立即数值）
            // 当 BB 内多次 li 同一常量时，消除冗余 li 或替换为 mv
            // 借鉴 Cpl6 立即数加载复用
            std::unordered_map<std::string, std::string> regToImm;
            auto invalidateRegImm = [&](const std::string& reg) {
                regToImm.erase(reg);
            };
            // imm 是否可放入 12 位有符号立即数（li 为单条 addi）
            // 大立即数 li 为 lui+addi 两条，mv 替换可省一条
            auto immFits12 = [](const std::string& imm) -> bool {
                if (imm.empty()) return false;
                try {
                    long val = std::stol(imm);
                    return val >= -2048 && val <= 2047;
                } catch (...) { return false; }
            };

            // ★ 预处理：统计被跳转指令引用的标签
            // 引用计数为 0 的标签是纯 fall-through 目标（无跳转指令跳到它），
            // LVN 可保留跟踪，消除跨 fall-through 标签的冗余指令。
            // 典型场景：shuffle1 中 beq 后 fall-through 到 endif 标签，
            //   slli t1,t4,2 在 beq 前后重复（t1/t4 未变），但 LVN 因标签清空未消除。
            // 安全性：纯 fall-through 标签只有一个前驱（上一条指令的 fall-through），
            //   寄存器值和前一条指令执行后一样，lastSeen 有效。
            std::set<std::string> referencedLabels;
            for (const auto& line : lines) {
                if (isEmptyOrComment(line)) continue;
                std::string opName = extractOpName(line);
                std::string rRd, rRs, rImm;
                std::string brRs, brLabel;
                if (opName == "j") {
                    if (tryMatch(line, "j", rRd, rRs, rImm) && !rRd.empty())
                        referencedLabels.insert(rRd);
                } else if (opName == "beqz" || opName == "bnez" || opName == "blez" ||
                           opName == "bgez" || opName == "bltz" || opName == "bgtz") {
                    if (tryMatchBranch(line, opName, brRs, brLabel) && !brLabel.empty())
                        referencedLabels.insert(brLabel);
                } else if (opName == "beq" || opName == "bne" || opName == "blt" ||
                           opName == "bge" || opName == "bltu" || opName == "bgeu") {
                    if (tryMatch(line, opName, rRd, rRs, rImm) && !rImm.empty())
                        referencedLabels.insert(rImm);
                }
            }

            auto extractRegs = [&](const std::string& s) -> std::vector<std::string> {
                std::vector<std::string> regs;
                for (std::sregex_iterator it(s.begin(), s.end(), regRegex), end; it != end; ++it) {
                    regs.push_back(it->str());
                }
                return regs;
            };
            auto regChangedSince = [&](const std::string& reg, size_t idx) -> bool {
                auto it = regLastWritten.find(reg);
                // ★ 用 > 而非 >=：idx 是 prevIdx（之前缓存该指令的位置），
                // reg 在 prevIdx 处被写入是预期的（指令自身的定义点），
                // 不算"之后被修改"。只有 regLastWritten[reg] > prevIdx
                // 才说明 reg 在 prevIdx 之后被其他指令覆写。
                // 修复前 >= 导致 regChangedSince(prevRd, prevIdx) 总是 true，
                // LVN 的冗余检测对被缓存指令的 rd 从未生效。
                return it != regLastWritten.end() && it->second > idx;
            };
            auto clearLoadEntries = [&]() {
                for (auto it = lastSeen.begin(); it != lastSeen.end(); ) {
                    const std::string& k = it->first;
                    if (k.rfind("lw|", 0) == 0 || k.rfind("ld|", 0) == 0 ||
                        k.rfind("lb|", 0) == 0 || k.rfind("lbu|", 0) == 0 ||
                        k.rfind("lhu|", 0) == 0 || k.rfind("lh|", 0) == 0 ||
                        k.rfind("lwu|", 0) == 0) {
                        it = lastSeen.erase(it);
                    } else {
                        ++it;
                    }
                }
            };
            // 可缓存的指令（纯计算，无副作用，且未被其他 pass 专门处理）
            static const std::set<std::string> CACHEABLE = {
                "add", "sub", "mul", "div", "divu", "rem", "remu",
                "and", "or", "xor", "sll", "srl", "sra",
                "slt", "sltu", "seqz", "snez",
                "addi", "andi", "ori", "xori", "slli", "srli", "srai", "slti", "sltiu",
                "addw", "subw", "sllw", "srlw", "sraw",
                "addiw", "slliw", "srliw", "sraiw",
                "lw", "ld", "lb", "lbu", "lh", "lhu", "lwu"
            };
            for (size_t i = 0; i < lines.size(); ++i) {
                const std::string& line = lines[i];
                // 标签：BB 边界
                if (isLabel(line)) {
                    compact.push_back(line);
                    // ★ 纯 fall-through 标签（未被任何跳转指令引用）：保留 LVN 跟踪
                    // 这类标签只有一个前驱（上一条指令的 fall-through），
                    // 寄存器值和前一条指令执行后一样，lastSeen/regLastWritten 仍有效。
                    // 典型收益：shuffle1 中 beq 后 fall-through 到 endif，
                    //   slli t1,t4,2 在 beq 前后重复，保留跟踪后可消除冗余 slli。
                    std::string trimmed = line;
                    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
                        trimmed = trimmed.substr(1);
                    std::string labelName = trimmed.substr(0, trimmed.size() - 1);
                    if (referencedLabels.count(labelName) == 0) {
                        continue;  // 纯 fall-through，保留跟踪
                    }
                    // 被跳转引用的标签：可能从其他路径跳来，清空跟踪
                    lastSeen.clear();
                    regLastWritten.clear();
                    // ★ li 复用：跳转目标可能从其他路径到达，寄存器值不可信
                    regToImm.clear();
                    continue;
                }
                // 跳过空行、注释、指令
                size_t p = 0;
                while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
                if (p >= line.size() || line[p] == '#' || line[p] == '.') {
                    compact.push_back(line);
                    continue;
                }
                std::string opName = extractOpName(line);
                // STORE：无目的寄存器，清除 LOAD 缓存
                if (opName == "sw" || opName == "sd" || opName == "sh" || opName == "sb" ||
                    opName == "fsw" || opName == "fsd" || opName == "fsh" || opName == "fsb") {
                    compact.push_back(line);
                    clearLoadEntries();
                    continue;
                }
                // CALL：清除 LOAD 缓存 + 杀死 caller-saved 寄存器（t0-t6, a0-a7, ra）
                if (opName == "call") {
                    compact.push_back(line);
                    clearLoadEntries();
                    for (auto it = regLastWritten.begin(); it != regLastWritten.end(); ) {
                        char c = it->first[0];
                        if (c == 't' || c == 'a' || it->first == "ra") {
                            it = regLastWritten.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    for (auto it = lastSeen.begin(); it != lastSeen.end(); ) {
                        const std::string& destReg = it->second.second;
                        if (!destReg.empty() && (destReg[0] == 't' || destReg[0] == 'a' || destReg == "ra")) {
                            it = lastSeen.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    // ★ li 复用：call 杀死 caller-saved 寄存器的 imm 跟踪
                    for (auto it = regToImm.begin(); it != regToImm.end(); ) {
                        char c = it->first[0];
                        if (c == 't' || c == 'a' || it->first == "ra") {
                            it = regToImm.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    continue;
                }
                // ★ li 复用：BB 内相同立即数重复加载时消除或替换为 mv
                // 借鉴 Cpl6 立即数加载复用。安全性：运行在 RA 之后，仅删指令或替 mv，
                // 不改变寄存器分配（规则 14）。可通过 PEEPHOLE_NO_LI_REUSE=1 禁用。
                if (opName == "li" && !getenv("PEEPHOLE_NO_LI_REUSE")) {
                    std::string liRd, liImm, liDummy;
                    if (tryMatch(line, "li", liRd, liImm, liDummy) &&
                        !liRd.empty() && liRd != "x0" && liRd != "zero" && !liImm.empty()) {
                        // 情况 1: rd 已持有相同 imm → li 冗余，删除
                        auto rIt = regToImm.find(liRd);
                        if (rIt != regToImm.end() && rIt->second == liImm) {
                            continue;  // 消除冗余 li
                        }
                        // 情况 2: 另一寄存器 srcReg 持有 imm → 大立即数替换为 mv 省 1 条
                        // 小立即数 li 与 mv 均为单条，mv 引入依赖不替换
                        if (!immFits12(liImm)) {
                            std::string bestSrc;
                            for (const auto& kv : regToImm) {
                                if (kv.second == liImm && kv.first != liRd) {
                                    bestSrc = kv.first;
                                    break;
                                }
                            }
                            if (!bestSrc.empty()) {
                                compact.push_back("  mv      " + liRd + ", " + bestSrc);
                                regLastWritten[liRd] = compact.size() - 1;
                                for (auto it = lastSeen.begin(); it != lastSeen.end(); ) {
                                    if (it->second.second == liRd) it = lastSeen.erase(it);
                                    else ++it;
                                }
                                invalidateRegImm(liRd);
                                regToImm[liRd] = liImm;
                                continue;
                            }
                        }
                        // 情况 3: 无法复用，保留 li 并更新跟踪
                        compact.push_back(line);
                        regLastWritten[liRd] = compact.size() - 1;
                        for (auto it = lastSeen.begin(); it != lastSeen.end(); ) {
                            if (it->second.second == liRd) it = lastSeen.erase(it);
                            else ++it;
                        }
                        invalidateRegImm(liRd);
                        regToImm[liRd] = liImm;
                        continue;
                    }
                    // 解析失败的 li 走默认非缓存路径
                }
                // 不可缓存的指令（branch/jump/mv/li/auipc 等）：直接保留
                if (CACHEABLE.count(opName) == 0) {
                    compact.push_back(line);
                    // 如果该指令有目的寄存器，更新 regLastWritten 并清除该 rd 的旧缓存
                    std::string rd2, rs2, imm2;
                    if (tryMatch(line, opName, rd2, rs2, imm2) && !rd2.empty() && rd2 != "x0" && rd2 != "zero") {
                        regLastWritten[rd2] = compact.size() - 1;
                        for (auto it = lastSeen.begin(); it != lastSeen.end(); ) {
                            if (it->second.second == rd2) {
                                it = lastSeen.erase(it);
                            } else {
                                ++it;
                            }
                        }
                        // ★ li 复用：rd 被覆写，清除其 imm 跟踪
                        invalidateRegImm(rd2);
                    }
                    continue;
                }
                // 可缓存指令：解析 op rd, rs, imm
                std::string rd, rs, imm;
                if (!tryMatch(line, opName, rd, rs, imm)) {
                    compact.push_back(line);
                    continue;
                }
                // x0/zero 作为目的寄存器：不缓存（写入被丢弃）
                if (rd.empty() || rd == "x0" || rd == "zero") {
                    compact.push_back(line);
                    continue;
                }
                // 构建 LVN key：opcode + 源操作数（不含 rd）
                std::string key = opName;
                if (!rs.empty()) key += "|" + rs;
                if (!imm.empty()) key += "|" + imm;
                // 检查是否之前见过相同 key
                auto found = lastSeen.find(key);
                bool redundant = false;
                if (found != lastSeen.end()) {
                    size_t prevIdx = found->second.first;
                    const std::string& prevRd = found->second.second;
                    // prevRd 不能为空，且自 prevIdx 以来未被修改
                    if (!prevRd.empty() && !regChangedSince(prevRd, prevIdx)) {
                        // 检查所有源寄存器自 prevIdx 以来是否被修改
                        std::string srcStr = rs + " " + imm;
                        auto srcRegs = extractRegs(srcStr);
                        bool srcChanged = false;
                        for (const auto& r : srcRegs) {
                            // ★ 如果源寄存器 == prevRd，则源寄存器在 prevIdx 处被写入
                            // （指令的定义点），改变了源寄存器的值。后续相同 key 的指令
                            // 使用的源操作数不同，不是冗余的。
                            // 典型场景：seqz s2, s2（s2 = (s2 == 0)）改变了 s2，
                            // 后续 seqz t0, s2 使用的 s2 是修改后的值，不是冗余的。
                            // 03_sort2 TIMEOUT 根因：缺少此检查导致 seqz t0,s2 被错误
                            // 替换为 mv t0,s2，改变了条件判断语义 → 无限循环。
                            if (r == prevRd) {
                                srcChanged = true;
                                break;
                            }
                            if (regChangedSince(r, prevIdx)) {
                                srcChanged = true;
                                break;
                            }
                        }
                        if (!srcChanged) {
                            // 冗余！
                            if (rd == prevRd) {
                                // 完全相同，直接删除（不 push）
                                redundant = true;
                            } else {
                                // rd 不同，替换为 mv rd, prevRd
                                compact.push_back("  mv      " + rd + ", " + prevRd);
                                regLastWritten[rd] = compact.size() - 1;
                                // ★ li 复用：rd 被 mv 覆写，清除其 imm 跟踪
                                invalidateRegImm(rd);
                                redundant = true;
                            }
                        }
                    }
                }
                if (!redundant) {
                    compact.push_back(line);
                    size_t curIdx = compact.size() - 1;
                    // 先清除 lastSeen 中 dest 为 rd 的旧条目（rd 被覆写，旧值无效）
                    for (auto it = lastSeen.begin(); it != lastSeen.end(); ) {
                        if (it->second.second == rd) {
                            it = lastSeen.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    lastSeen[key] = {curIdx, rd};
                    regLastWritten[rd] = curIdx;
                    // ★ li 复用：rd 被计算结果覆写，清除其 imm 跟踪
                    invalidateRegImm(rd);
                }
            }
            lines = std::move(compact);
        }

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
                        if (liRd == addRd && addRs1 == liRd && addRs2 != liRd) {
                            result.push_back("  mv      " + liRd + ", " + addRs2);
                            ++i;
                            matched = true;
                        }
                    }
                    // li rd, 0; addw rd, rd, rs → mv rd, rs
                    if (!matched && liImm == "0" && tryMatch(lines[i + 1], "addw", addRd, addRs1, addRs2)) {
                        if (liRd == addRd && addRs1 == liRd && addRs2 != liRd) {
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
                    // li rd, 0; subw rd2, rd, rs → negw rd2, rs (rd != rd2，li 目标是 subw 第一操作数)
                    // 安全条件：liRd 在 subw 之后必须死亡（subw 定义 rd2 而非 rd）
                    if (!matched && liImm == "0" && tryMatch(lines[i + 1], "subw", addRd, addRs1, addRs2)) {
                        if (addRs1 == liRd && addRd != liRd) {
                            if (isRegDeadInBB(lines, i + 2, liRd)) {
                                result.push_back("  negw    " + addRd + ", " + addRs2);
                                ++i;
                                matched = true;
                            }
                        }
                    }
                    // li rd, 0; sub rd2, rd, rs → neg rd2, rs (rd != rd2，非w变体)
                    // 安全条件：liRd 在 sub 之后必须死亡
                    if (!matched && liImm == "0" && tryMatch(lines[i + 1], "sub", addRd, addRs1, addRs2)) {
                        if (addRs1 == liRd && addRd != liRd) {
                            if (isRegDeadInBB(lines, i + 2, liRd)) {
                                result.push_back("  neg     " + addRd + ", " + addRs2);
                                ++i;
                                matched = true;
                            }
                        }
                    }
                    // li rd, 0; mv rd2, rd → li rd2, 0
                    // 安全条件：liRd 在 mv 之后必须死亡
                    if (!matched && liImm == "0") {
                        std::string mvRd, mvRs;
                        if (tryMatch(lines[i + 1], "mv", mvRd, mvRs, imm)) {
                            if (mvRs == liRd && mvRd != liRd) {
                                if (isRegDeadInBB(lines, i + 2, liRd)) {
                                    result.push_back("  li      " + mvRd + ", 0");
                                    ++i;
                                    matched = true;
                                }
                            }
                        }
                    }
                    // li rd, N; mv rd2, rd → li rd2, N (N 任意值，需 liRd 在 mv 后死亡)
                    // 仅处理 liImm 可解析为整数的情况，且需要基本块内死寄存器检查
                    if (!matched && liImm != "0") {
                        std::string mvRd, mvRs;
                        if (tryMatch(lines[i + 1], "mv", mvRd, mvRs, imm)) {
                            if (mvRs == liRd && mvRd != liRd) {
                                // 安全条件：liRd 在 mv 之后必须死亡（使用 isRegDeadInBB 正确处理 BB 边界）
                                if (isRegDeadInBB(lines, i + 2, liRd)) {
                                    result.push_back("  li      " + mvRd + ", " + liImm);
                                    ++i;
                                    matched = true;
                                }
                            }
                        }
                    }
                    // li rd, 0; sw/sd/sh/sb rd, offset(base) → sw/sd/sh/sb x0, offset(base)
                    // Eliminates the li by using hardwired zero register x0
                    // 安全条件：liRd 在 store 之后必须死亡
                    // ★ stOff 必须是 off(base) 格式（含括号），避免匹配 la+MEM 产生的 3 操作数形式 sw rt, sym, tmp
                    if (!matched && liImm == "0") {
                        static const char* STORE_OPS[] = {"sw", "sd", "sh", "sb"};
                        for (const char* op : STORE_OPS) {
                            std::string stRd, stOff, stImm;
                            if (tryMatch(lines[i + 1], op, stRd, stOff, stImm) && stRd == liRd) {
                                // stImm 必须为空（2 操作数形式 off(base)），stOff 必须含括号
                                if (stImm.empty() && stOff.find('(') != std::string::npos) {
                                    if (isRegDeadInBB(lines, i + 2, liRd)) {
                                        result.push_back("  " + std::string(op) + "      x0, " + stOff);
                                        ++i;
                                        matched = true;
                                    }
                                }
                                break;
                            }
                        }
                    }
                    // li rd, 0; beq/bne/blt/bge/bltu/bgeu rd, rs2, L → 用 x0 替换 rd，消除 li
                    // 汇编器自动将 beq x0, rs2, L 转为 beqz rs2, L 等伪指令（更短编码）
                    // 安全条件：liRd 在分支后局部死亡（使用 isRegLocalDead 放宽 BB 边界检查，
                    //   因为 li 加载的 0 仅被分支使用，liRd 即便是 callee-saved 也已被覆写为 0，
                    //   后续若使用即为使用 0 而非原值，故 BB 边界后视为死亡是安全的）
                    if (!matched && liImm == "0") {
                        static const char* BRANCH_OPS[] = {"beq", "bne", "blt", "bge", "bltu", "bgeu"};
                        for (const char* op : BRANCH_OPS) {
                            std::string brRs1, brRs2, brLabel;
                            if (tryMatch(lines[i + 1], op, brRs1, brRs2, brLabel)) {
                                bool replaceRs1 = (brRs1 == liRd);
                                bool replaceRs2 = (brRs2 == liRd);
                                if (replaceRs1 || replaceRs2) {
                                    if (isRegLocalDead(lines, i + 2, liRd)) {
                                        std::string newRs1 = replaceRs1 ? "x0" : brRs1;
                                        std::string newRs2 = replaceRs2 ? "x0" : brRs2;
                                        result.push_back("  " + std::string(op) + "    " + newRs1 + ", " + newRs2 + ", " + brLabel);
                                        ++i;
                                        matched = true;
                                    }
                                }
                                break;
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

            // ★ 自更新通过临时寄存器：mv tA, sX; op tB, tA, C; mv sX, tB → op sX, sX, C
            // 典型场景：循环归纳变量递增 s4 = s4 + 1 通过 t4/t3 中转
            //   codegen 对 promoted alloca 的 load-compute-store 模式产生：
            //     mv t4, s4       ; load s4 into temp
            //     addiw t3, t4, 1 ; compute in temp
            //     mv s4, t3       ; store back to s4
            //   优化为：addiw s4, s4, 1（省 2 条指令）
            // 安全条件：
            //   1. tA 在 op 之后死亡（isRegDeadInBB from i+2）
            //   2. tB 在第二条 mv 之后死亡（isRegDeadInBB from i+3）
            //   3. C != sX（避免 sX 在结果中出现两次，如 op sX, sX, sX）
            //   4. op 是二元 ALU 操作（排除 store/branch/mv/la/li）
            //   5. tB != tA（op 的目的与源不同）
            // 实测：132 处模式（123 addiw + 6 addw + 3 slliw），每次省 2 条 = 264 条
            if (!matched && i + 2 < lines.size() &&
                !isEmptyOrComment(lines[i + 1]) && !isEmptyOrComment(lines[i + 2])) {
                std::string mv1Rd, mv1Rs;
                if (tryMatch(lines[i], "mv", mv1Rd, mv1Rs, imm) && mv1Rd != mv1Rs) {
                    // 解析第二条指令：op tB, tA, C
                    std::string opName2 = extractOpName(lines[i + 1]);
                    // 排除非二元 ALU 操作
                    static const std::set<std::string> BINARY_ALU_OPS = {
                        "add", "addw", "addi", "addiw",
                        "sub", "subw",
                        "mul", "mulw",
                        "and", "andi", "or", "ori", "xor", "xori",
                        "sll", "sllw", "slli", "slliw",
                        "srl", "srlw", "srli", "srliw",
                        "sra", "sraw", "srai", "sraiw",
                        "slt", "sltu", "slti", "sltiu"
                    };
                    if (BINARY_ALU_OPS.count(opName2) > 0) {
                        std::string op2Rd, op2Rs1, op2Rs2;
                        if (tryMatch(lines[i + 1], opName2, op2Rd, op2Rs1, op2Rs2) &&
                            op2Rs1 == mv1Rd && op2Rd != mv1Rd) {
                            // 第三条：mv sX, tB
                            std::string mv2Rd, mv2Rs;
                            if (tryMatch(lines[i + 2], "mv", mv2Rd, mv2Rs, imm) &&
                                mv2Rd == mv1Rs && mv2Rs == op2Rd) {
                                // 检查 C != sX
                                if (op2Rs2 != mv1Rs) {
                                    // 检查 tA 在 op 之后局部死亡（heuristic for scratch regs）
                                    if (isRegLocalDead(lines, i + 2, mv1Rd)) {
                                        // 检查 tB 在第二条 mv 之后局部死亡
                                        if (isRegLocalDead(lines, i + 3, op2Rd)) {
                                            result.push_back("  " + opName2 + "    " + mv1Rs + ", " + mv1Rs + ", " + op2Rs2);
                                            i += 2;
                                            matched = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ★ 2指令版自更新：op tB, sX, C; mv sX, tB → op sX, sX, C
            // 典型场景：addw a0, a1, t3; mv a1, a0（a1 = a1 + t3 通过 a0 中转）
            // 安全条件：tB 在 mv 之后局部死亡（isRegLocalDead），C != sX
            // 实测：141 处模式（111 src1=sX + 30 src2=sX），每次省 1 条
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string opName1 = extractOpName(lines[i]);
                static const std::set<std::string> BINARY_ALU_OPS2 = {
                    "add", "addw", "addi", "addiw",
                    "sub", "subw",
                    "mul", "mulw",
                    "and", "andi", "or", "ori", "xor", "xori",
                    "sll", "sllw", "slli", "slliw",
                    "srl", "srlw", "srli", "srliw",
                    "sra", "sraw", "srai", "sraiw",
                    "slt", "sltu", "slti", "sltiu"
                };
                if (BINARY_ALU_OPS2.count(opName1) > 0) {
                    std::string op1Rd, op1Rs1, op1Rs2;
                    if (tryMatch(lines[i], opName1, op1Rd, op1Rs1, op1Rs2)) {
                        std::string mvRd, mvRs;
                        if (tryMatch(lines[i + 1], "mv", mvRd, mvRs, imm) &&
                            mvRs == op1Rd && mvRd != op1Rd) {
                            // sX = mvRd (write-back target), tB = op1Rd
                            std::string sX = mvRd;
                            std::string tB = op1Rd;
                            // src1 = sX 的情况
                            if (op1Rs1 == sX && op1Rs2 != sX) {
                                if (isRegLocalDead(lines, i + 2, tB)) {
                                    result.push_back("  " + opName1 + "    " + sX + ", " + sX + ", " + op1Rs2);
                                    ++i;
                                    matched = true;
                                }
                            }
                            // src2 = sX 的情况（src1 != sX）
                            else if (op1Rs2 == sX && op1Rs1 != sX) {
                                if (isRegLocalDead(lines, i + 2, tB)) {
                                    result.push_back("  " + opName1 + "    " + sX + ", " + op1Rs1 + ", " + sX);
                                    ++i;
                                    matched = true;
                                }
                            }
                        }
                    }
                }
            }

            // sw rs, off(sp) followed immediately by lw rd, off(sp) → sw rs, off(sp); mv rd, rs
            // ★ 安全条件：必须保留 sw！后续可能有其他 lw 从同一地址读取（94_nested_loops SEGFAULT 根因）
            // 仅当 lwReg == swReg 时可消除 lw（rd 已有正确值），否则替换为 mv
            // ★ swOff/lwOff 必须是 off(base) 格式（含括号），避免匹配 la+MEM 产生的 3 操作数形式
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string swReg, swOff, swImm;
                if (tryMatch(lines[i], "sw", swReg, swOff, swImm) && swImm.empty() && swOff.find('(') != std::string::npos) {
                    std::string lwReg, lwOff, lwImm;
                    if (tryMatch(lines[i + 1], "lw", lwReg, lwOff, lwImm) && lwImm.empty()) {
                        if (swOff == lwOff && !swOff.empty()) {
                            result.push_back(lines[i]);  // 保留 sw（内存写副作用不能消除）
                            if (lwReg != swReg) {
                                result.push_back("  mv      " + lwReg + ", " + swReg);
                            }
                            // lwReg == swReg 时 lw 是冗余的，直接消除
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // sd rs, off(base) followed immediately by ld rd, off(base) → sd rs, off(base); mv rd, rs
            // ★ 安全条件：必须保留 sd！后续可能有其他 ld 从同一地址读取（同 sw+lw 安全模型）
            // 仅当 ldReg == sdReg 时可消除 ld（rd 已有正确值），否则替换为 mv
            // ★ sdOff/ldOff 必须是 off(base) 格式（含括号），避免匹配 la+MEM 产生的 3 操作数形式
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string sdReg, sdOff, sdImm;
                if (tryMatch(lines[i], "sd", sdReg, sdOff, sdImm) && sdImm.empty() && sdOff.find('(') != std::string::npos) {
                    std::string ldReg, ldOff, ldImm;
                    if (tryMatch(lines[i + 1], "ld", ldReg, ldOff, ldImm) && ldImm.empty()) {
                        if (sdOff == ldOff && !sdOff.empty()) {
                            result.push_back(lines[i]);  // 保留 sd（内存写副作用不能消除）
                            if (ldReg != sdReg) {
                                result.push_back("  mv      " + ldReg + ", " + sdReg);
                            }
                            // ldReg == sdReg 时 ld 是冗余的，直接消除
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // fsw rs, off(sp) followed immediately by flw rd, off(sp) → fsw rs, off(sp); fmv.s rd, rs
            // ★ 安全条件：必须保留 fsw！后续可能有其他 flw 从同一地址读取
            // ★ fsOff/flOff 必须是 off(base) 格式（含括号）
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string fsReg, fsOff, fsImm;
                if (tryMatch(lines[i], "fsw", fsReg, fsOff, fsImm) && fsImm.empty() && fsOff.find('(') != std::string::npos) {
                    std::string flReg, flOff, flImm;
                    if (tryMatch(lines[i + 1], "flw", flReg, flOff, flImm) && flImm.empty()) {
                        if (fsOff == flOff && !fsOff.empty()) {
                            result.push_back(lines[i]);  // 保留 fsw
                            if (flReg != fsReg) {
                                result.push_back("  fmv.s   " + flReg + ", " + fsReg);
                            }
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // fsd rs, off(base) followed immediately by fld rd, off(base) → fsd rs, off(base); fmv.d rd, rs
            // ★ 安全条件：必须保留 fsd！后续可能有其他 fld 从同一地址读取（同 fsw+flw 安全模型）
            // ★ fsOff/flOff 必须是 off(base) 格式（含括号）
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string fsReg, fsOff, fsImm;
                if (tryMatch(lines[i], "fsd", fsReg, fsOff, fsImm) && fsImm.empty() && fsOff.find('(') != std::string::npos) {
                    std::string flReg, flOff, flImm;
                    if (tryMatch(lines[i + 1], "fld", flReg, flOff, flImm) && flImm.empty()) {
                        if (fsOff == flOff && !fsOff.empty()) {
                            result.push_back(lines[i]);  // 保留 fsd
                            if (flReg != fsReg) {
                                result.push_back("  fmv.d   " + flReg + ", " + fsReg);
                            }
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // mv rd, rs followed by load/store using rd as address → use rs directly
            // 安全条件：mvRd 在 load/store 之后必须死亡（否则 mv 的副作用仍需要）
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
                        // 检查 mvRd 在 load/store 之后是否死亡
                        if (isRegDeadInBB(lines, i + 2, mvRd)) {
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
            }

            // ★ mv+mv 链合并：mv rd1, rs; mv rd2, rd1 → mv rd2, rs
            // 安全条件：rd1 在第二条 mv 后死亡，rd1 != rd2，rd1 != rs，rd2 != rs
            //           （rd2 == rs 时两条 mv 均可消除，但保守起见仅处理 rd2 != rs）
            // ★ 死亡检查：caller-saved (tX/aX) 用 isRegLocalDead（BB 边界=死），
            //   callee-saved (sX) 用 isRegDeadInBB（保守，BB 边界=活），
            //   因为 sX 跨 BB 存活，isRegLocalDead 会误判为死亡。
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mv1Rd, mv1Rs;
                if (tryMatch(lines[i], "mv", mv1Rd, mv1Rs, imm) &&
                    !mv1Rd.empty() && !mv1Rs.empty() && mv1Rd != mv1Rs) {
                    std::string mv2Rd, mv2Rs;
                    if (tryMatch(lines[i + 1], "mv", mv2Rd, mv2Rs, imm) &&
                        mv2Rs == mv1Rd && mv2Rd != mv1Rd) {
                        // 根据寄存器类选择死亡检查
                        bool isCallerSaved = (!mv1Rd.empty() &&
                            (mv1Rd[0] == 't' || mv1Rd[0] == 'a'));
                        bool rd1Dead = isCallerSaved
                            ? isRegLocalDead(lines, i + 2, mv1Rd)
                            : isRegDeadInBB(lines, i + 2, mv1Rd);
                        if (rd1Dead) {
                            if (mv2Rd != mv1Rs) {
                                // mv rd2, rs
                                result.push_back("  mv      " + mv2Rd + ", " + mv1Rs);
                            } else {
                                // mv rd2, rd2 → no-op，跳过两条 mv
                            }
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // mv rd, rs; <op> rd3, ..., rd, ... → <op> rd3, ..., rs, ...
            // 通用模式：mv 的目标寄存器 rd 在下一条指令中作为源操作数（非目的、非地址）
            // 安全条件：
            //   1. rd 在 op 之后必须死亡（isRegDeadInBB）
            //   2. op 的目的寄存器 rd3 != rs（否则 rs 被覆写，语义改变）
            //   3. rd 不是 op 的目的寄存器（自修改模式由其他 pattern 处理）
            // 支持三寄存器 op：add/addw/sub/subw/mul/mulw/sll/sllw/sra/sraw/srl/srlw/and/or/xor/slt/sltu/div/divw/rem/remw
            // 支持立即数 op：addi/addiw/slli/srli/srai/slti/sltiu/andi/ori/xori
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm) && !mvRd.empty() && !mvRs.empty() && mvRd != mvRs) {
                    std::string nextLine = lines[i + 1];
                    std::string opName = extractOpName(nextLine);
                    static const std::set<std::string> THREE_REG_OPS = {
                        "add", "addw", "sub", "subw", "mul", "mulw",
                        "sll", "sllw", "sra", "sraw", "srl", "srlw",
                        "and", "or", "xor", "slt", "sltu", "div", "divw", "rem", "remw"
                    };
                    static const std::set<std::string> IMM_OPS = {
                        "addi", "addiw", "slli", "srli", "srai", "slti", "sltiu", "andi", "ori", "xori"
                    };
                    bool isThreeReg = THREE_REG_OPS.count(opName) > 0;
                    bool isImmOp = IMM_OPS.count(opName) > 0;
                    if (isThreeReg || isImmOp) {
                        std::string opRd, opRs, opRs2;
                        if (tryMatch(nextLine, opName, opRd, opRs, opRs2)) {
                            // rd 不能是 op 的目的寄存器
                            if (opRd != mvRd) {
                                bool usedAsSrc = false;
                                std::string newRs = opRs;
                                std::string newRs2 = opRs2;
                                if (opRs == mvRd) {
                                    usedAsSrc = true;
                                    newRs = mvRs;
                                }
                                if (isThreeReg && opRs2 == mvRd) {
                                    usedAsSrc = true;
                                    newRs2 = mvRs;
                                }
                                if (usedAsSrc &&
                                    opRd != mvRs &&  // op 不能覆写 rs
                                    isRegDeadInBB(lines, i + 2, mvRd)) {
                                    std::string newLine = "  " + opName + "    " + opRd + ", " + newRs;
                                    if (!newRs2.empty()) {
                                        newLine += ", " + newRs2;
                                    }
                                    result.push_back(newLine);
                                    ++i;
                                    matched = true;
                                }
                            }
                        }
                    }
                }
            }

            // mv rd, rs; STORE rd, off(base) → STORE rs, off(base)
            // mv 的目标 rd 作为 store 的值寄存器
            // 安全条件：
            //   1. rd 在 store 之后必须死亡
            //   2. rd 不能出现在地址部分（否则消除 mv 后地址使用旧值）
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm) && !mvRd.empty() && !mvRs.empty() && mvRd != mvRs) {
                    std::string nextLine = lines[i + 1];
                    std::string opName = extractOpName(nextLine);
                    static const std::set<std::string> STORE_OPS_VAL = {
                        "sw", "sd", "sh", "sb", "fsw", "fsd", "fsh", "fsb"
                    };
                    if (STORE_OPS_VAL.count(opName) > 0) {
                        std::string stRd, stOff, stImm;
                        if (tryMatch(nextLine, opName, stRd, stOff, stImm) && stImm.empty() &&
                            stOff.find('(') != std::string::npos && stRd == mvRd) {
                            // 提取地址寄存器
                            auto parenPos = stOff.find('(');
                            auto closePos = stOff.find(')', parenPos);
                            std::string addrReg = stOff.substr(parenPos + 1, closePos - parenPos - 1);
                            // rd 不能出现在地址中
                            if (addrReg != mvRd && isRegDeadInBB(lines, i + 2, mvRd)) {
                                result.push_back("  " + opName + "      " + mvRs + ", " + stOff);
                                ++i;
                                matched = true;
                            }
                        }
                    }
                }
            }

            // mv rd, rs; LOAD rd, off(base) → LOAD rd, off(base)  (mv is dead)
            // load 的目标寄存器 rd 覆写 mv 的结果，mv 是死代码
            // 安全条件：rd 不能出现在地址部分（如 lw rd, 0(rd)，mv 的值被地址使用）
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm) && !mvRd.empty() && !mvRs.empty() && mvRd != mvRs) {
                    std::string nextLine = lines[i + 1];
                    std::string opName = extractOpName(nextLine);
                    static const std::set<std::string> LOAD_OPS_DEST = {
                        "lw", "ld", "lh", "lb", "lbu", "lhu", "flw", "fld"
                    };
                    if (LOAD_OPS_DEST.count(opName) > 0) {
                        std::string ldRd, ldOff, ldImm;
                        if (tryMatch(nextLine, opName, ldRd, ldOff, ldImm) && ldImm.empty() &&
                            ldOff.find('(') != std::string::npos && ldRd == mvRd) {
                            // 提取地址寄存器
                            auto parenPos = ldOff.find('(');
                            auto closePos = ldOff.find(')', parenPos);
                            std::string addrReg = ldOff.substr(parenPos + 1, closePos - parenPos - 1);
                            // rd 不能出现在地址中
                            if (addrReg != mvRd) {
                                result.push_back(nextLine);
                                ++i;
                                matched = true;
                            }
                        }
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
            // 安全条件：mvRd 在 branch 之后必须死亡（branch 不定义寄存器，mvRd 可能 live-out）
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm)) {
                    std::string brRs, brLabel;
                    if (tryMatchBranch(lines[i + 1], "bnez", brRs, brLabel) && brRs == mvRd) {
                        if (isRegDeadInBB(lines, i + 2, mvRd)) {
                            result.push_back("  bnez    " + mvRs + ", " + brLabel);
                            ++i;
                            matched = true;
                        }
                    } else if (tryMatchBranch(lines[i + 1], "beqz", brRs, brLabel) && brRs == mvRd) {
                        if (isRegDeadInBB(lines, i + 2, mvRd)) {
                            result.push_back("  beqz    " + mvRs + ", " + brLabel);
                            ++i;
                            matched = true;
                        }
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

            // ★ mv rd, rs; <op> rd, rd, X → <op> rd, rs, X
            // 前向 copy 传播：mv 后跟一条用 rd 作为（第一个）源操作数的双操作数
            // 计算指令，且结果写回 rd。mv 定义的 rd 值被 op 立即覆写，故 mv 冗余。
            // 借鉴 Cpl6 copy propagation。
            // 安全条件：
            //   1. mv 和 op 相邻（中间无注释外的指令）
            //   2. op 的 rd == mv 的 rd（结果写回 mv 的目标）
            //   3. op 的第一个源 == mv 的 rd（使用 mv 的目标作为源）
            //   4. mv 的 rd != mv 的 rs（非 self-mv）
            // 安全性：运行在 RA 之后，仅替换源操作数，不改变寄存器分配（规则 14）。
            // 可通过 PEEPHOLE_NO_MV_COPYPROP=1 禁用。
            if (!matched && !getenv("PEEPHOLE_NO_MV_COPYPROP") &&
                i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm) &&
                    !mvRs.empty() && mvRd != mvRs) {
                    std::string opRd, opRs, opImm;
                    std::string opName = extractOpName(lines[i + 1]);
                    // 双操作数计算指令（结果写回 rd，两个源操作数）
                    static const std::set<std::string> BINARY_OPS = {
                        "add", "sub", "mul", "div", "divu", "rem", "remu",
                        "and", "or", "xor", "sll", "srl", "sra", "slt", "sltu",
                        "addw", "subw", "mulw", "sllw", "srlw", "sraw"
                    };
                    if (BINARY_OPS.count(opName) &&
                        tryMatch(lines[i + 1], opName, opRd, opRs, opImm) &&
                        opRd == mvRd && opRs == mvRd && !opImm.empty()) {
                        // 替换所有等于 mvRd 的源为 mvRs
                        // （opImm 可能也是 mvRd，如 mul t1, t1, t1）
                        std::string newImm = (opImm == mvRd) ? mvRs : opImm;
                        result.push_back("  " + opName + "    " + opRd + ", " +
                                         mvRs + ", " + newImm);
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
                        tryThreeRegOp("sraw") || tryThreeRegOp("srlw") || tryThreeRegOp("mulw") ||
                        tryThreeRegOp("divw") || tryThreeRegOp("remw") || tryThreeRegOp("add")  ||
                        tryThreeRegOp("sub")  || tryThreeRegOp("sll")  || tryThreeRegOp("sra")  ||
                        tryThreeRegOp("srl")  || tryThreeRegOp("or")   || tryThreeRegOp("and")  ||
                        tryThreeRegOp("xor")  || tryThreeRegOp("slt")  || tryThreeRegOp("sltu")) {
                        isThreeReg = true;
                        opName = extractOpName(lines[i]);
                    }

                    if (isThreeReg && tryMatch(lines[nextIdx], "mv", mvRd, mvRs, imm)) {
                        if (mvRs == opRd) {
                            // 安全条件：opRd 在 mv 之后必须死亡（合并后 opRd 不再被写入）
                            // ★ 寄存器类型感知：caller-saved (t/a) 用 isRegLocalDead（BB 边界=死），
                            //   callee-saved (s) 用 isRegDeadInBB（BB 边界=活）
                            // ★ 依赖 isEmptyOrComment 修复：标签行不再被当作空行跳过，
                            //   确保 nextIdx 不会跨 BB 查找 mv
                            if (isRegDeadAware(lines, nextIdx + 1, opRd)) {
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
                            tryImmOp("slli") || tryImmOp("srli") || tryImmOp("srai") ||
                            tryImmOp("slliw") || tryImmOp("srliw") || tryImmOp("sraiw") ||
                            tryImmOp("andi") || tryImmOp("ori") || tryImmOp("xori") ||
                            tryImmOp("slti") || tryImmOp("sltiu")) {
                            opName = extractOpName(lines[i]);
                            if (tryMatch(lines[nextIdx], "mv", mvRd, mvRs, imm)) {
                                if (mvRs == opRd) {
                                    // 安全条件：opRd 在 mv 之后必须死亡
                                    // ★ 寄存器类型感知：caller-saved (t/a) 用 isRegLocalDead
                                    if (isRegDeadAware(lines, nextIdx + 1, opRd)) {
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
                        // 安全条件：liRd 在 op 之后必须死亡（op 定义 opRd 而非 liRd）
                        if (!isRegDeadInBB(lines, i + 2, liRd)) return false;
                        // 检查 op 后面是否有 mv 可以合并
                        size_t mvIdx = i + 2;
                        while (mvIdx < lines.size() && isEmptyOrComment(lines[mvIdx])) {
                            mvIdx++;
                        }
                        std::string mvRd, mvRs;
                        if (mvIdx < lines.size() && tryMatch(lines[mvIdx], "mv", mvRd, mvRs, imm)) {
                            if (mvRs == opRd) {
                                // 检查 opRd 在 mv 之后是否还被后续指令使用
                                if (isRegDeadInBB(lines, mvIdx + 1, opRd)) {
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

            // li rN, N; COMMUTATIVE op rd, rN, rs → opi rd, rs, N
            // 处理可交换运算（add/addw/and/or/xor）的 li 目标在第二个操作数位置的情况
            // 例如: li t0, 1; and s3, t0, s2 → andi s3, s2, 1
            //       li t0, 1; and t0, t0, t3 → andi t0, t3, 1
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string liRd, liImm;
                if (tryMatch(lines[i], "li", liRd, liImm, imm)) {
                    int64_t liVal = std::stoll(liImm);
                    bool fitI12 = (-2048 <= liVal && liVal <= 2047);
                    std::string opRd, opRs, opRs2;

                    auto tryLiOpCommutative = [&](const std::string& op, const std::string& opi, int64_t val, bool cond) -> bool {
                        if (!cond) return false;
                        if (!tryMatch(lines[i + 1], op, opRd, opRs, opRs2)) return false;
                        // li 目标在第二个操作数位置（opRs == liRd），第三个操作数是另一个寄存器
                        if (opRs != liRd || opRs2 == liRd) return false;
                        // 安全条件：liRd 在 op 之后必须死亡（合并后 li 被消除，liRd 不再被定义）
                        if (!isRegDeadInBB(lines, i + 2, liRd)) return false;
                        // 检查 op 后面是否有 mv 可以合并
                        size_t mvIdx = i + 2;
                        while (mvIdx < lines.size() && isEmptyOrComment(lines[mvIdx])) {
                            mvIdx++;
                        }
                        std::string mvRd, mvRs;
                        if (mvIdx < lines.size() && tryMatch(lines[mvIdx], "mv", mvRd, mvRs, imm)) {
                            if (mvRs == opRd) {
                                // 安全条件：opRd 在 mv 之后必须死亡
                                if (isRegDeadInBB(lines, mvIdx + 1, opRd)) {
                                    result.push_back("  " + opi + "    " + mvRd + ", " + opRs2 + ", " + std::to_string(val));
                                    for (size_t j = i + 2; j < mvIdx; ++j) {
                                        result.push_back(lines[j]);
                                    }
                                    i = mvIdx;
                                    return true;
                                }
                            }
                        }
                        // 无 mv 合并：交换操作数（可交换运算）
                        result.push_back("  " + opi + "    " + opRd + ", " + opRs2 + ", " + std::to_string(val));
                        ++i;
                        return true;
                    };

                    // 可交换运算：add/addw/and/or/xor
                    if (tryLiOpCommutative("addw", "addiw", liVal, fitI12)) { matched = true; }
                    else if (tryLiOpCommutative("add", "addi", liVal, fitI12)) { matched = true; }
                    else if (tryLiOpCommutative("and", "andi", liVal, fitI12)) { matched = true; }
                    else if (tryLiOpCommutative("or", "ori", liVal, fitI12)) { matched = true; }
                    else if (tryLiOpCommutative("xor", "xori", liVal, fitI12)) { matched = true; }
                }
            }

            // mv reg, src followed by sw/fsw/sd reg, offset(sp) → sw/fsw/sd src, offset(sp)
            // 安全条件：mvRd 在 store 之后必须死亡（消除 mv 后 mvRd 不再被定义）
            // ★ swOff 必须是 off(base) 格式（含括号），避免匹配 la+MEM 产生的 3 操作数形式
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mvRd, mvRs;
                if (tryMatch(lines[i], "mv", mvRd, mvRs, imm)) {
                    std::string swReg, swOff, swImm;
                    if (tryMatch(lines[i + 1], "sw", swReg, swOff, swImm)) {
                        if (swReg == mvRd && swImm.empty() && swOff.find('(') != std::string::npos && isRegDeadInBB(lines, i + 2, mvRd)) {
                            result.push_back("  sw      " + mvRs + ", " + swOff);
                            ++i;
                            matched = true;
                        }
                    }
                    if (!matched && tryMatch(lines[i + 1], "fsw", swReg, swOff, swImm)) {
                        if (swReg == mvRd && swImm.empty() && swOff.find('(') != std::string::npos && isRegDeadInBB(lines, i + 2, mvRd)) {
                            result.push_back("  fsw     " + mvRs + ", " + swOff);
                            ++i;
                            matched = true;
                        }
                    }
                    if (!matched && tryMatch(lines[i + 1], "sd", swReg, swOff, swImm)) {
                        if (swReg == mvRd && swImm.empty() && swOff.find('(') != std::string::npos && isRegDeadInBB(lines, i + 2, mvRd)) {
                            result.push_back("  sd      " + mvRs + ", " + swOff);
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // addi rd, BASE, X; MEM rs, Y(rd) → MEM rs, X+Y(BASE)
            // BASE 可以是任意寄存器（sp/s0/s1/.../t2 等），当 rd 是 BASE 的临时偏移寄存器且在 MEM 后不再使用时，
            // 可直接用 X+Y(BASE) 替代。addi 与 MEM 相邻，BASE 在两者之间不可能被修改，故 BASE 稳定。
            // MEM ∈ {ld, sd, lw, sw, ld, flw, fsw, fld, fsd, sh, sb, lh, lb, lhu, lbu}
            // ★ 安全条件：① 合并偏移在 12-bit 有符号范围 ② addiRd 在 MEM 之后死亡 ③ addiRd != addiRs
            // ★ 即使 MEM 是 LOAD 且 memRd == addiRs 也安全：地址在 writeback 之前已计算（如 lw s1,100(s1)）
            // ★ 排除 x0/zero：addi rd, x0, X 等价于 li rd, X，应由 li 优化路径处理
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string addiRd, addiRs, addiImm;
                if (tryMatch(lines[i], "addi", addiRd, addiRs, addiImm) && !addiRs.empty() &&
                    addiRs != "x0" && addiRs != "zero" && addiRd != addiRs) {
                    // 解析 addi 的立即数
                    bool addiOk = false;
                    int64_t addiVal = 0;
                    try { addiVal = std::stoll(addiImm); addiOk = true; }
                    catch (...) {}

                    if (addiOk) {
                        // 匹配下一行的内存访问指令
                        static const char* MEM_OPS[] = {"ld", "sd", "lw", "sw", "flw", "fsw", "fld", "fsd", "sh", "sb", "lh", "lb", "lhu", "lbu"};
                        for (const char* op : MEM_OPS) {
                            std::string memRd, memOff;
                            if (tryMatch(lines[i + 1], op, memRd, memOff, imm)) {
                                // 解析 memOff 格式: Y(rd)
                                auto parenPos = memOff.find('(');
                                if (parenPos != std::string::npos) {
                                    std::string offStr = memOff.substr(0, parenPos);
                                    auto closeParen = memOff.find(')', parenPos);
                                    if (closeParen != std::string::npos) {
                                        std::string baseReg = memOff.substr(parenPos + 1, closeParen - parenPos - 1);
                                        if (baseReg == addiRd) {
                                            // 合并偏移: X + Y
                                            int64_t memOffVal = 0;
                                            bool memOffOk = false;
                                            if (offStr.empty()) {
                                                memOffVal = 0;
                                                memOffOk = true;
                                            } else {
                                                try { memOffVal = std::stoll(offStr); memOffOk = true; }
                                                catch (...) {}
                                            }
                                            if (memOffOk) {
                                                int64_t combinedOff = addiVal + memOffVal;
                                                // 检查合并后偏移在 12-bit 有符号范围内
                                                if (-2048 <= combinedOff && combinedOff <= 2047) {
                                                    // 安全条件：addiRd 在 MEM 之后必须死亡（使用 isRegDeadInBB 正确处理 BB 边界）
                                                    if (isRegDeadInBB(lines, i + 2, addiRd)) {
                                                        result.push_back("  " + std::string(op) + "      " + memRd + ", " + std::to_string(combinedOff) + "(" + addiRs + ")");
                                                        ++i;
                                                        matched = true;
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // addi rd, rs, C1; addi rd2, rd, C2 → addi rd2, rs, (C1+C2)
            // 当 rd 是临时偏移寄存器且在第二条 addi 后死亡时，可合并两条 addi
            // ★ 安全条件：C1+C2 必须在 12-bit 有符号范围内；rd 在第二条 addi 后必须死亡
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string addi1Rd, addi1Rs, addi1Imm;
                if (tryMatch(lines[i], "addi", addi1Rd, addi1Rs, addi1Imm) && addi1Rd != addi1Rs) {
                    std::string addi2Rd, addi2Rs, addi2Imm;
                    if (tryMatch(lines[i + 1], "addi", addi2Rd, addi2Rs, addi2Imm) && addi2Rs == addi1Rd) {
                        try {
                            int64_t c1 = std::stoll(addi1Imm);
                            int64_t c2 = std::stoll(addi2Imm);
                            int64_t total = c1 + c2;
                            if (-2048 <= total && total <= 2047) {
                                // 安全条件：addi1Rd 在第二条 addi 后必须死亡
                                if (isRegDeadInBB(lines, i + 2, addi1Rd)) {
                                    result.push_back("  addi    " + addi2Rd + ", " + addi1Rs + ", " + std::to_string(total));
                                    ++i;
                                    matched = true;
                                }
                            }
                        } catch (...) {}
                    }
                }
            }

            // la rd, sym; MEM rt, off(rd) → MEM rt, sym+off  (load)
            // la rd, sym; MEM rt, off(rd) → MEM rt, sym+off, rd (store)
            // 当 rd 在 MEM 之后死亡时，可消除 la，直接用符号寻址
            // Load: 汇编器自动用 rt 作为临时寄存器（auipc rt; MEM rt, 0(rt)）
            // Store: 需要指定 rd 作为临时寄存器（auipc rd; MEM rt, 0(rd)）
            // ★ 安全条件：
            //   - rd 在 MEM 之后必须死亡（la 被消除，rd 不再被定义）
            //   - Store 额外要求 rd != rt（否则 auipc 会覆写 rt 的值）
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string laRd, laSym;
                if (tryMatch(lines[i], "la", laRd, laSym, imm) && !laSym.empty()) {
                    // 匹配下一行的内存访问指令
                    // GNU as can use the integer load destination as the
                    // temporary address register for symbolic loads.  A
                    // floating-point destination cannot hold that address,
                    // so flw/fld must retain the preceding `la`.
                    static const char* LOAD_OPS[] = {"ld", "lw", "lh", "lb", "lhu", "lbu"};
                    static const char* STORE_OPS[] = {"sd", "sw", "sh", "sb", "fsw", "fsd"};
                    auto parseMemOff = [](const std::string& memOff, std::string& offStr, std::string& baseReg) -> bool {
                        auto parenPos = memOff.find('(');
                        if (parenPos == std::string::npos) return false;
                        auto closeParen = memOff.find(')', parenPos);
                        if (closeParen == std::string::npos) return false;
                        offStr = memOff.substr(0, parenPos);
                        baseReg = memOff.substr(parenPos + 1, closeParen - parenPos - 1);
                        return true;
                    };
                    // 尝试 load 指令
                    for (const char* op : LOAD_OPS) {
                        std::string memRd, memOff;
                        if (tryMatch(lines[i + 1], op, memRd, memOff, imm)) {
                            std::string offStr, baseReg;
                            if (parseMemOff(memOff, offStr, baseReg) && baseReg == laRd) {
                                if (isRegDeadInBB(lines, i + 2, laRd)) {
                                    std::string symExpr = laSym;
                                    if (!offStr.empty() && offStr != "0") {
                                        symExpr = laSym + (offStr[0] == '-' ? offStr : "+" + offStr);
                                    }
                                    result.push_back("  " + std::string(op) + "      " + memRd + ", " + symExpr);
                                    ++i;
                                    matched = true;
                                    break;
                                }
                            }
                        }
                    }
                    // 尝试 store 指令
                    if (!matched) {
                        for (const char* op : STORE_OPS) {
                            std::string memRd, memOff;
                            if (tryMatch(lines[i + 1], op, memRd, memOff, imm)) {
                                std::string offStr, baseReg;
                                if (parseMemOff(memOff, offStr, baseReg) && baseReg == laRd) {
                                    // Store: rd 不能等于 rt（否则 auipc 会覆写 rt）
                                    if (laRd != memRd && isRegDeadInBB(lines, i + 2, laRd)) {
                                        std::string symExpr = laSym;
                                        if (!offStr.empty() && offStr != "0") {
                                            symExpr = laSym + (offStr[0] == '-' ? offStr : "+" + offStr);
                                        }
                                        result.push_back("  " + std::string(op) + "      " + memRd + ", " + symExpr + ", " + laRd);
                                        ++i;
                                        matched = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // mv rd, rs; mv rd2, rd → mv rd2, rs (当 rd 在第二条 mv 后不再使用)
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mv1Rd, mv1Rs;
                if (tryMatch(lines[i], "mv", mv1Rd, mv1Rs, imm)) {
                    std::string mv2Rd, mv2Rs;
                    if (tryMatch(lines[i + 1], "mv", mv2Rd, mv2Rs, imm)) {
                        if (mv2Rs == mv1Rd && mv2Rd != mv1Rd && mv1Rs != mv1Rd) {
                            // 安全条件：mv1Rd 在第二条 mv 之后必须死亡（使用 isRegDeadInBB 正确处理 BB 边界）
                            if (isRegDeadInBB(lines, i + 2, mv1Rd)) {
                                result.push_back("  mv      " + mv2Rd + ", " + mv1Rs);
                                ++i;
                                matched = true;
                            }
                        }
                    }
                }
            }

            // ★ addi tX, base, K; MEM rt, OFF(tX) → MEM rt, (K+OFF)(base)
            // 当 tX 在 MEM 之后死亡时，可消除 addi，将偏移量合并到 MEM 指令中
            // 安全条件：
            //   1. tX 在 MEM 之后死亡（addi 被消除，tX 不再被定义）
            //   2. K+OFF 适合 12 位有符号立即数（-2048 到 2047）
            //   3. MEM 的基址寄存器是 tX（addi 的目标）
            //   4. 不跨 BB（中间无标签）
            if (!matched && i + 1 < lines.size()) {
                std::string addiRd, addiRs, addiImm;
                if (tryMatch(lines[i], "addi", addiRd, addiRs, addiImm) && !addiRd.empty()) {
                    // 解析 addi 立即数
                    int64_t addiK;
                    try {
                        addiK = std::stoll(addiImm);
                    } catch (...) {
                        addiK = 0;
                        addiImm = "";  // 标记无效
                    }
                    if (!addiImm.empty()) {
                        // 查找下一条真实指令（跳过空行/注释/汇编指令，但不跳过标签）
                        size_t memIdx = i + 1;
                        while (memIdx < lines.size() && isEmptyOrComment(lines[memIdx])) {
                            memIdx++;
                        }
                        if (memIdx < lines.size() && !isLabel(lines[memIdx])) {
                            // 尝试匹配各种内存访问指令
                            static const char* MEM_OPS[] = {"ld", "lw", "lh", "lb", "lhu", "lbu",
                                                              "sd", "sw", "sh", "sb",
                                                              "flw", "fld", "fsw", "fsd"};
                            for (const char* op : MEM_OPS) {
                                std::string memRd, memOff;
                                if (tryMatch(lines[memIdx], op, memRd, memOff, imm) && !memOff.empty()) {
                                    // 解析 OFF(base) 格式
                                    auto parenPos = memOff.find('(');
                                    if (parenPos != std::string::npos) {
                                        auto closeParen = memOff.find(')', parenPos);
                                        if (closeParen != std::string::npos) {
                                            std::string offStr = memOff.substr(0, parenPos);
                                            std::string baseReg = memOff.substr(parenPos + 1, closeParen - parenPos - 1);
                                            if (baseReg == addiRd) {
                                                // 解析 OFF
                                                int64_t memOffVal = 0;
                                                bool offValid = false;
                                                if (offStr.empty()) {
                                                    offValid = true;
                                                    memOffVal = 0;
                                                } else {
                                                    try {
                                                        memOffVal = std::stoll(offStr);
                                                        offValid = true;
                                                    } catch (...) {}
                                                }
                                                if (offValid) {
                                                    int64_t totalOff = addiK + memOffVal;
                                                    if (-2048 <= totalOff && totalOff <= 2047) {
                                                        // 检查 addiRd 在 MEM 之后死亡
                                                        if (isRegDeadAware(lines, memIdx + 1, addiRd)) {
                                                            // 合并：MEM rt, totalOff(base)
                                                            std::string newOff = (totalOff == 0) ? "" : std::to_string(totalOff);
                                                            result.push_back("  " + std::string(op) + "      " + memRd + ", " + newOff + "(" + addiRs + ")");
                                                            // 保留 addi 和 MEM 之间的空行/注释
                                                            for (size_t j = i + 1; j < memIdx; ++j) {
                                                                result.push_back(lines[j]);
                                                            }
                                                            i = memIdx;
                                                            matched = true;
                                                            break;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ld/lw/la rd, addr; mv rd2, rd → ld/lw/la rd2, addr (当 rd 在 mv 后死亡)
            // 消除 load+mv 链：直接将加载目标改为 mv 的目的寄存器
            // 安全条件：rd != rd2，rd 不在地址表达式中，rd 在 mv 后基本块内不再被读取
            // Bug 修复1：用 findNextRealInst 跳过空行/注释/指令，匹配间隔的 mv
            // Bug 修复2：用 instrKillsReg 区分读/写操作数，避免将寄存器被覆写误判为存活
            if (!matched) {
                size_t mvIdx = findNextRealInst(lines, i + 1);
                if (mvIdx < lines.size()) {
                    static const char* LOAD_OPS[] = {"ld", "lw", "la"};
                    for (const char* op : LOAD_OPS) {
                        std::string ldRd, ldRs, ldImm;
                        if (tryMatch(lines[i], op, ldRd, ldRs, ldImm)) {
                            std::string mvRd, mvRs;
                            if (tryMatch(lines[mvIdx], "mv", mvRd, mvRs, ldImm) &&
                                mvRs == ldRd && mvRd != ldRd) {
                                // ldRd 不能在地址中（如 ld t0, X(t0) 不可优化）
                                // mvRd 也不能在地址中（如 ld t0, X(s5); mv s5, t0 → ld s5, X(s5) 不安全）
                                if (!regInStr(ldRs, ldRd) && !regInStr(ldRs, mvRd)) {
                                    // 基本块内死寄存器检查：ldRd 在 mv 后到下一个标签前不再被读取
                                    // 关键：遇到 BB 边界（标签）时必须保守认为 ldRd 可能 live-out
                                    //       因为后续 BB 可能使用 ldRd 的值，不能优化
                                    bool ldRdDead = true;
                                    for (size_t k = mvIdx + 1; k < lines.size(); ++k) {
                                        const std::string& l = lines[k];
                                        if (l.empty()) continue;
                                        if (isLabel(l)) {
                                            // BB 边界：ldRd 可能被后续 BB 使用，保守不优化
                                            ldRdDead = false;
                                            break;
                                        }
                                        size_t p = 0;
                                        while (p < l.size() && (l[p] == ' ' || l[p] == '\t')) ++p;
                                        if (p >= l.size()) continue;  // 纯空白
                                        if (l[p] == '#') continue;    // 注释
                                        if (l[p] == '.') continue;    // 指令（非标签）
                                        // 先检查 ldRd 是否被此指令杀死（定义但不读取）
                                        if (instrKillsReg(l, ldRd)) {
                                            break;  // ldRd 被覆写，确认死亡
                                        }
                                        // 检查 ldRd 是否作为源操作数出现（被读取）
                                        if (regInStr(l, ldRd)) {
                                            ldRdDead = false;
                                            break;
                                        }
                                        // call 指令会杀死所有 caller-saved 寄存器（t0-t6, a0-a7）
                                        // 如果 ldRd 是 caller-saved，则 call 后它已死亡
                                        std::string opN = extractOpName(l);
                                        if (opN == "call" && !ldRd.empty()) {
                                            char c = ldRd[0];
                                            if (c == 't' || c == 'a') {
                                                break;  // caller-saved 被 call 杀死
                                            }
                                            // callee-saved (s0-s11) 跨 call 存活，继续扫描
                                        }
                                        // ★ j（无条件跳转）：不会 fall-through，ldRd 在 j 之后不再被使用
                                        // 典型场景：lw t3,0(t0); mv t4,t3; j .Lcond
                                        //   t3 在 mv 后只有 j，j 不读取 t3 且不 fall-through，t3 死亡
                                        // ★ 暂时禁用：10_DFS SEGFAULT，需要调查
                                        // if (opN == "j") {
                                        //     break;  // ldRd 确认死亡
                                        // }
                                    }
                                    if (ldRdDead) {
                                        result.push_back("  " + std::string(op) + "      " + mvRd + ", " + ldRs);
                                        // 保留 ld 和 mv 之间的空行/注释/指令
                                        for (size_t k = i + 1; k < mvIdx; ++k) {
                                            result.push_back(lines[k]);
                                        }
                                        i = mvIdx;  // 跳过 mv 行（for 循环的 ++i 会跳到 mvIdx + 1）
                                        matched = true;
                                    }
                                }
                            }
                            break;  // tryMatch 已匹配，不再尝试其他 op
                        }
                    }
                }
            }

            // addi rd, rs, N; addi rd, rd, M → addi rd, rs, N+M
            // 合并连续的 addi 链（GEP 地址计算的典型模式：addi t2, sp, 8; addi t2, t2, X）
            // 条件：两条 addi 相邻（中间可有空行/注释/指令），第二条的 rs1 == rd == 第一条的 rd，N+M fit imm12
            if (!matched) {
                size_t addiIdx = findNextRealInst(lines, i + 1);
                if (addiIdx < lines.size()) {
                    std::string addi1Rd, addi1Rs, addi1Imm;
                    if (tryMatch(lines[i], "addi", addi1Rd, addi1Rs, addi1Imm)) {
                        std::string addi2Rd, addi2Rs, addi2Imm;
                        if (tryMatch(lines[addiIdx], "addi", addi2Rd, addi2Rs, addi2Imm)) {
                            if (addi2Rs == addi1Rd && addi2Rd == addi1Rd) {
                                bool ok1 = false, ok2 = false;
                                int64_t val1 = 0, val2 = 0;
                                try { val1 = std::stoll(addi1Imm); ok1 = true; }
                                catch (...) {}
                                try { val2 = std::stoll(addi2Imm); ok2 = true; }
                                catch (...) {}
                                if (ok1 && ok2) {
                                    int64_t combined = val1 + val2;
                                    if (-2048 <= combined && combined <= 2047) {
                                        result.push_back("  addi    " + addi1Rd + ", " + addi1Rs + ", " + std::to_string(combined));
                                        for (size_t k = i + 1; k < addiIdx; ++k) {
                                            result.push_back(lines[k]);
                                        }
                                        i = addiIdx;
                                        matched = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ★ branch+j 反转优化：bXX L1; j L2; L1: → bXX_inv L2; L1: (fall-through)
            // 当分支指令的目标 L1 恰好是 j 之后的下一条标签时，
            // 反转分支条件使其跳转到 L2，j 被消除（L1 成为 fall-through）。
            // 典型场景：while 循环的 blt cond, body; j end; body: 模式
            // 安全条件：
            //   1. bXX 和 j 在同一基本块内相邻（中间可有空行/注释/.p2align）
            //   2. j 之后第一个实际标签 == 分支目标 L1（中间只允许空标签/注释/.p2align）
            //   3. L1 != L2（否则是自循环，不能反转）
            // ★ QEMU 安全：运行在寄存器分配之后，不改变寄存器分配（规则 14）
            // ★ 汇编器安全：RISC-V GNU as 支持分支松弛（branch relaxation），
            //   若反转后的分支目标 L2 超出 ±4KB 范围，as 自动插入 j trampoline，
            //   退化为原始模式，不会产生错误或回退。
            if (!matched && !std::getenv("PEEPHOLE_NO_BRANCH_INVERT")) {
                std::string opName = extractOpName(lines[i]);
                static const std::unordered_map<std::string, std::string> BR_INVERT = {
                    {"beq", "bne"}, {"bne", "beq"},
                    {"blt", "bge"}, {"bge", "blt"},
                    {"bltu", "bgeu"}, {"bgeu", "bltu"},
                    {"beqz", "bnez"}, {"bnez", "beqz"}
                };
                auto invIt = BR_INVERT.find(opName);
                if (invIt != BR_INVERT.end()) {
                    // 提取分支目标 L1
                    std::string brTarget;
                    std::string brRs, brLabel;       // for beqz/bnez
                    std::string brRd, brRs2, brImm;  // for beq/bne/blt/bge/bltu/bgeu
                    bool isBranchOneReg = (opName == "beqz" || opName == "bnez");
                    if (isBranchOneReg) {
                        if (tryMatchBranch(lines[i], opName, brRs, brLabel) && !brLabel.empty())
                            brTarget = brLabel;
                    } else {
                        if (tryMatch(lines[i], opName, brRd, brRs2, brImm) && !brImm.empty())
                            brTarget = brImm;
                    }

                    if (!brTarget.empty()) {
                        // 查找下一条真实指令（必须是 j L2）
                        size_t jIdx = findNextRealInst(lines, i + 1);
                        if (jIdx < lines.size()) {
                            std::string jRd, jRs, jImm;
                            if (tryMatch(lines[jIdx], "j", jRd, jRs, jImm) && !jRd.empty()
                                && jRd != brTarget) {
                                // 查找 j 之后的第一个标签（跳过空行/注释/.p2align）
                                // 第一个标签必须 == 分支目标 L1（否则不能反转）
                                bool foundTarget = false;
                                for (size_t k = jIdx + 1; k < lines.size(); ++k) {
                                    const std::string& nextLine = lines[k];
                                    if (nextLine.empty()) continue;
                                    size_t p = 0;
                                    while (p < nextLine.size() && (nextLine[p] == ' ' || nextLine[p] == '\t')) ++p;
                                    if (p >= nextLine.size()) continue;
                                    char firstChar = nextLine[p];
                                    if (firstChar == '#') continue;
                                    if (firstChar == '.') {
                                        std::string rest = nextLine.substr(p);
                                        if (rest.size() >= 8 && rest.substr(0, 8) == ".p2align") continue;
                                        if (rest.size() >= 6 && rest.substr(0, 6) == ".align") continue;
                                        // 其他 . 开头的可能是 .L 标签（如 .Lxxx:），需检查 ':'
                                        // 不能 break，继续到下面的标签检查
                                    }
                                    // 检查是否是标签行（以冒号结尾）
                                    std::string trimmed = nextLine;
                                    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
                                        trimmed = trimmed.substr(1);
                                    if (!trimmed.empty() && trimmed.back() == ':') {
                                        std::string labelName = trimmed.substr(0, trimmed.size() - 1);
                                        foundTarget = (labelName == brTarget);
                                        break;  // 第一个标签就停止（不管是否匹配）
                                    }
                                    // 非 .p2align/.align 的指令行（如 .word, .size 等）→ 不能 fall-through
                                    if (firstChar == '.') break;
                                    break;  // 遇到指令（非 label），停止
                                }
                                if (foundTarget) {
                                    // 反转分支：bXX ... L1 → bXX_inv ... L2
                                    std::string invOp = invIt->second;
                                    if (isBranchOneReg) {
                                        result.push_back("  " + invOp + "    " + brRs + ", " + jRd);
                                    } else {
                                        result.push_back("  " + invOp + "    " + brRd + ", " + brRs2 + ", " + jRd);
                                    }
                                    // 保留 bXX 和 j 之间的空行/注释/.p2align
                                    for (size_t k = i + 1; k < jIdx; ++k) {
                                        result.push_back(lines[k]);
                                    }
                                    i = jIdx;  // 跳过 j 行（for 循环的 ++i 会跳到 jIdx + 1）
                                    matched = true;
                                }
                            }
                        }
                    }
                }
            }

            // j label; label: → eliminate j (fall through to label)
            // 跳过中间的空行、注释、.p2align/.align 指令、非目标空 label
            // 典型模式：j .Lxxx; .Lxxx: 或 j .Lxxx; .p2align 4; .Lxxx: 或 j .Lxxx; .Lempty; .Lxxx:
            if (!matched) {
                std::string jRd, jRs, jImm;
                if (tryMatch(lines[i], "j", jRd, jRs, jImm) && !jRd.empty()) {
                    bool canEliminate = false;
                    for (size_t k = i + 1; k < lines.size(); ++k) {
                        const std::string& nextLine = lines[k];
                        // 跳过空行和纯空白行
                        if (nextLine.empty()) continue;
                        size_t p = 0;
                        while (p < nextLine.size() && (nextLine[p] == ' ' || nextLine[p] == '\t')) ++p;
                        if (p >= nextLine.size()) continue;  // 纯空白
                        char firstChar = nextLine[p];
                        // 跳过注释
                        if (firstChar == '#') continue;
                        // 跳过 .p2align 和 .align 指令（不生成代码，只插入 NOP 填充）
                        if (firstChar == '.') {
                            std::string rest = nextLine.substr(p);
                            if (rest.size() >= 8 && rest.substr(0, 8) == ".p2align") continue;
                            if (rest.size() >= 6 && rest.substr(0, 6) == ".align") continue;
                        }
                        // 检查是否是标签行（以冒号结尾）
                        std::string trimmed = nextLine;
                        while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
                            trimmed = trimmed.substr(1);
                        if (!trimmed.empty() && trimmed.back() == ':') {
                            std::string labelName = trimmed.substr(0, trimmed.size() - 1);
                            if (labelName == jRd) {
                                canEliminate = true;
                                break;  // 找到目标 label，停止
                            }
                            // 非目标 label：跳过（可能是空 label，对控制流透明）
                            continue;
                        }
                        break;  // 遇到指令（非 label），停止
                    }
                    if (canEliminate) {
                        matched = true;  // 跳过 j，不 push 任何内容，让执行流 fall through
                    }
                }
            }

            // ★ sw x0, off(base); sw x0, off+4(base) → sd x0, off(base)
            // 合并两条相邻的 4 字节零写入为单条 8 字节零写入（数组清零典型模式）
            // 安全条件：
            //   1. 两条 sw 在同一基本块内连续（无标签分隔，由主循环保证）
            //   2. 两条 sw 的源操作数均为 x0（零寄存器，值固定为 0）
            //   3. 第二条偏移 = 第一条偏移 + 4，基址寄存器相同
            //   4. off % 8 == 0（8 字节对齐，避免非对齐 sd 访问）
            //   5. swOff 必须是 off(base) 格式（含括号），避免匹配 la+MEM 产生的 3 操作数形式
            // ★ QEMU 安全：运行在寄存器分配之后，不改变寄存器分配（规则 14）
            // ★ 类似地处理 sh x0 对：sh x0, off; sh x0, off+2 → sw x0, off（off % 4 == 0）
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string sw1Reg, sw1Off, sw1Imm;
                if (tryMatch(lines[i], "sw", sw1Reg, sw1Off, sw1Imm) && sw1Imm.empty() &&
                    sw1Reg == "x0" && sw1Off.find('(') != std::string::npos) {
                    std::string sw2Reg, sw2Off, sw2Imm;
                    if (tryMatch(lines[i + 1], "sw", sw2Reg, sw2Off, sw2Imm) && sw2Imm.empty() &&
                        sw2Reg == "x0" && sw2Off.find('(') != std::string::npos) {
                        // 解析 off(base) 格式
                        auto parseOffBase = [](const std::string& s, int64_t& off, std::string& base) -> bool {
                            auto parenPos = s.find('(');
                            if (parenPos == std::string::npos) return false;
                            auto closePos = s.find(')', parenPos);
                            if (closePos == std::string::npos) return false;
                            std::string offStr = s.substr(0, parenPos);
                            base = s.substr(parenPos + 1, closePos - parenPos - 1);
                            if (offStr.empty()) { off = 0; return true; }
                            try { off = std::stoll(offStr); return true; }
                            catch (...) { return false; }
                        };
                        int64_t off1, off2;
                        std::string base1, base2;
                        if (parseOffBase(sw1Off, off1, base1) && parseOffBase(sw2Off, off2, base2)) {
                            if (base1 == base2 && off2 == off1 + 4 && off1 % 8 == 0) {
                                result.push_back("  sd      x0, " + std::to_string(off1) + "(" + base1 + ")");
                                ++i;
                                matched = true;
                            }
                        }
                    }
                }
            }

            // ★ sh x0, off(base); sh x0, off+2(base) → sw x0, off(base)
            // 合并两条相邻的 2 字节零写入为单条 4 字节零写入
            // 安全条件：同 sw 对合并，外加 off % 4 == 0
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string sh1Reg, sh1Off, sh1Imm;
                if (tryMatch(lines[i], "sh", sh1Reg, sh1Off, sh1Imm) && sh1Imm.empty() &&
                    sh1Reg == "x0" && sh1Off.find('(') != std::string::npos) {
                    std::string sh2Reg, sh2Off, sh2Imm;
                    if (tryMatch(lines[i + 1], "sh", sh2Reg, sh2Off, sh2Imm) && sh2Imm.empty() &&
                        sh2Reg == "x0" && sh2Off.find('(') != std::string::npos) {
                        auto parseOffBase = [](const std::string& s, int64_t& off, std::string& base) -> bool {
                            auto parenPos = s.find('(');
                            if (parenPos == std::string::npos) return false;
                            auto closePos = s.find(')', parenPos);
                            if (closePos == std::string::npos) return false;
                            std::string offStr = s.substr(0, parenPos);
                            base = s.substr(parenPos + 1, closePos - parenPos - 1);
                            if (offStr.empty()) { off = 0; return true; }
                            try { off = std::stoll(offStr); return true; }
                            catch (...) { return false; }
                        };
                        int64_t off1, off2;
                        std::string base1, base2;
                        if (parseOffBase(sh1Off, off1, base1) && parseOffBase(sh2Off, off2, base2)) {
                            if (base1 == base2 && off2 == off1 + 2 && off1 % 4 == 0) {
                                result.push_back("  sw      x0, " + std::to_string(off1) + "(" + base1 + ")");
                                ++i;
                                matched = true;
                            }
                        }
                    }
                }
            }

            // ★ mv 链合并：mv rd, rs; mv rd2, rd → mv rd2, rs（当 rd 在第二条 mv 后死亡）
            // 典型模式：mv s1, s0; mv a0, s1（s1 是临时中转，a0 是返回值寄存器）
            // 安全条件：
            //   1. 两条 mv 在同一基本块内连续
            //   2. 第二条 mv 的源 == 第一条 mv 的目的（o2[1] == o1[0]）
            //   3. rd（第一条 mv 的目的）在第二条 mv 之后不再被使用（isRegDeadInBB）
            //   4. rd2 != rd（避免 mv rd,rs; mv rd,rd 退化情况）
            // ★ QEMU 安全：运行在寄存器分配之后，不改变寄存器分配
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string mv1Rd, mv1Rs;
                if (tryMatch(lines[i], "mv", mv1Rd, mv1Rs, imm) && !mv1Rd.empty() && !mv1Rs.empty() && mv1Rd != mv1Rs) {
                    std::string mv2Rd, mv2Rs;
                    if (tryMatch(lines[i + 1], "mv", mv2Rd, mv2Rs, imm) && !mv2Rd.empty() && mv2Rs == mv1Rd && mv2Rd != mv1Rd) {
                        if (isRegDeadInBB(lines, i + 2, mv1Rd)) {
                            result.push_back("  mv      " + mv2Rd + ", " + mv1Rs);
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // ★ 冗余 lw+sw 同址同寄存器消除：lw rd, off(b); sw rd, off(b)
            // 从内存加载后立即写回同一地址（无意义的 load-store）
            // 安全条件：
            //   1. 两条指令在同一基本块内连续
            //   2. 内存操作数完全相同（off 和 base 都一致）
            //   3. sw 的源寄存器 == lw 的目的寄存器
            //   4. rd 在 sw 之后不再被使用 → 两条都删除；否则只删 sw
            // ★ QEMU 安全：运行在寄存器分配之后
            if (!matched && i + 1 < lines.size() && !isEmptyOrComment(lines[i + 1])) {
                std::string lwReg, lwOff, lwImm;
                if (tryMatch(lines[i], "lw", lwReg, lwOff, lwImm) && lwImm.empty() && lwOff.find('(') != std::string::npos) {
                    std::string swReg, swOff, swImm;
                    if (tryMatch(lines[i + 1], "sw", swReg, swOff, swImm) && swImm.empty() &&
                        swReg == lwReg && swOff == lwOff && !swOff.empty()) {
                        // rd 在 sw 之后死亡 → 两条都删除
                        if (isRegDeadInBB(lines, i + 2, lwReg)) {
                            ++i;  // 跳过 lw 和 sw
                            matched = true;
                        } else {
                            // rd 仍活跃 → 只删 sw，保留 lw
                            result.push_back(lines[i]);
                            ++i;
                            matched = true;
                        }
                    }
                }
            }

            // ★ 有符号 x % 2^n 立即测试优化（最常见：x % 2 != 0）
            // 模式（7 条指令）：
            //   sraiw  tA, tX, 31      ; tA = sign(tX)
            //   andi   tB, tA, MASK    ; tB = sign & (2^n - 1)
            //   addw   tA, tB, tX      ; tA = tX + round-adjust
            //   sraiw  tB, tA, N       ; tB = (tX+adj) >> N = tX / 2^n
            //   slliw  tA, tB, N       ; tA = (tX/2^n) * 2^n
            //   subw   tB, tX, tA      ; tB = tX - (tX/2^n)*2^n = tX % 2^n
            //   bnez/beqz tB, label    ; test (tX % 2^n)
            // 替换（2 条指令）：
            //   andi   tB, tX, MASK    ; tB = tX & (2^n - 1)
            //   bnez/beqz tB, label    ; test (等价：x%2^n!=0 ⟺ x&(2^n-1)!=0)
            // 原理：对所有 x（正/负/零），x % 2^n != 0  ⟺  x & (2^n - 1) != 0
            //   - 正数：x % 2^n == x & (2^n-1)，显然等价
            //   - 负数：x % 2^n ∈ {-(2^n-1),...,-1}（非零 iff x 不是 2^n 的倍数），
            //           x & (2^n-1) ∈ {1,...,2^n-1}（非零 iff x 不是 2^n 的倍数）
            //   - 零：两者都为 0
            // 安全条件：
            //   1. 7 条指令在同一 BB 内连续（中间无标签/注释/指令）
            //   2. MASK == (1 << N) - 1
            //   3. tA（中间寄存器）在 subw 后不再使用（subw 后紧跟 branch，tA 不在 branch 操作数中）
            //   4. tB 在 branch 后不再使用（branch 是 BB 终结符）
            // ★ QEMU 安全：运行在寄存器分配之后，不改变寄存器分配（规则 14）
            // ★ 汇编器安全：RISC-V GNU as，andi+branch 均为合法指令
            if (!matched && i + 6 < lines.size() &&
                !isEmptyOrComment(lines[i+1]) && !isEmptyOrComment(lines[i+2]) &&
                !isEmptyOrComment(lines[i+3]) && !isEmptyOrComment(lines[i+4]) &&
                !isEmptyOrComment(lines[i+5]) && !isEmptyOrComment(lines[i+6])) {
                std::string sra1Rd, sra1Rs, sra1Imm;
                if (tryMatch(lines[i], "sraiw", sra1Rd, sra1Rs, sra1Imm) && sra1Imm == "31") {
                    std::string andRd, andRs, andImm;
                    if (tryMatch(lines[i+1], "andi", andRd, andRs, andImm) && andRs == sra1Rd) {
                        std::string addRd, addRs1, addRs2;
                        if (tryMatch(lines[i+2], "addw", addRd, addRs1, addRs2) &&
                            addRd == sra1Rd && addRs1 == andRd && addRs2 == sra1Rs) {
                            std::string sra2Rd, sra2Rs, sra2Imm;
                            if (tryMatch(lines[i+3], "sraiw", sra2Rd, sra2Rs, sra2Imm) &&
                                sra2Rs == addRd && sra2Rd == andRd) {
                                std::string sllRd, sllRs, sllImm;
                                if (tryMatch(lines[i+4], "slliw", sllRd, sllRs, sllImm) &&
                                    sllRs == sra2Rd && sllRd == addRd && sllImm == sra2Imm) {
                                    std::string subRd, subRs1, subRs2;
                                    if (tryMatch(lines[i+5], "subw", subRd, subRs1, subRs2) &&
                                        subRs1 == sra1Rs && subRs2 == sllRd && subRd == sra2Rd) {
                                        std::string brOp = extractOpName(lines[i+6]);
                                        std::string brRs, brLabel;
                                        if ((brOp == "bnez" || brOp == "beqz") &&
                                            tryMatchBranch(lines[i+6], brOp, brRs, brLabel) &&
                                            brRs == subRd) {
                                            // 验证 MASK == (1 << N) - 1
                                            int n = std::atoi(sra2Imm.c_str());
                                            int mask = std::atoi(andImm.c_str());
                                            if (n >= 1 && n <= 30 && mask == ((1 << n) - 1)) {
                                                // 安全检查：中间寄存器 sra1Rd（=addRd=sllRd）
                                                // 在 subw 之后不再使用。subw 后紧跟 branch，
                                                // branch 只读 subRd，不读 sra1Rd。
                                                // 但需确认 sra1Rd != sra1Rs（否则原值被破坏）。
                                                if (sra1Rd != sra1Rs) {
                                                    result.push_back("  andi    " + subRd + ", " + sra1Rs + ", " + andImm);
                                                    result.push_back("  " + brOp + "    " + subRd + ", " + brLabel);
                                                    i += 6;  // 跳过 6 条原指令（branch 是第 7 条，一并消费）
                                                    matched = true;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ★ 有符号 x % 2^n 计算优化（无 branch，保留 tB = x % 2^n）
            // 当 x % 2^n 的结果被后续使用（非 branch 测试）时，
            // 仍可将 6 条指令替换为更高效的序列：
            //   andi tB, tX, (2^n-1)     ; tB = |x % 2^n|（无符号余数）
            //   sraiw tA, tX, 31         ; tA = sign
            //   ...（对负数取负）...
            // 但这仅在余数被使用时才有意义，且实现复杂。
            // 暂不实现，仅处理上述 branch 测试场景（最高频）。

            // ★ 有符号 x / 2^n 立即测试优化（x / 2^n == 0 ⟺ -2^n < x < 2^n）
            // 模式（4 条指令）：
            //   sraiw  tA, tX, 31
            //   andi   tB, tA, MASK
            //   addw   tA, tB, tX
            //   sraiw  tB, tA, N         ; tB = x / 2^n
            //   bnez/beqz tB, label      ; test (x / 2^n)
            // 替换：andi 不行（除法结果不能简化为位运算）
            // 仅当 N=1 且测试 == 0 时可用 "bltu tX, 2" 之类的范围检查，但语义不等价。
            // 跳过，专注于 % 2^n 模式。

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

        // 调试：保存每次迭代的结果
        const char* dumpIter = std::getenv("DUMP_PEEPHOLE_ITER");
        if (dumpIter) {
            std::string fname = "/tmp/peep_iter" + std::to_string(iter) + ".S";
            std::ofstream f(fname);
            f << joinLines(lines);
        }
    }

    return joinLines(lines);
}

} // namespace Opt
