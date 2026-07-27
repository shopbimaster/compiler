// ================================================================
// Post-RA 局部指令调度（汇编层）
// ----------------------------------------------------------------
// 作用于 TargetCodeGen 生成的最终汇编文本。IR 层调度已实测被 regalloc/
// codegen 抵消（load-use 停顿零改善）；汇编层看到真实物理寄存器与真实
// 相邻关系，重排不会被下游抵消，是唯一能真正隐藏 RISC-V load-use 延迟的层次。
//
// 正确性第一：
//   - 未识别助记符 → 该块整块放弃调度（绝不猜 def/use）。
//   - call / 控制流 / 指示符 / sp,ra 相关 → 硬边界或整块跳过。
//   - 内存依赖保守：load 不跨越前面的 store；store 严格保序（无别名分析）。
//   - 所有 tie-break 用原始行序，保证决定性。
//
// 收益只有真实流水线（平台）能验证——qemu 不模拟延迟。本地仅以
// "load-use 相邻对" 弱代理评估。SCHED_OFF=1 可关闭（对照/回退）。
// ================================================================

#include "backend/PostRAScheduler.h"
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Backend {
namespace {

// ---- 一条解析后的指令行 ----
struct AsmInst {
    std::string raw;                 // 原始整行（回写用，保留原格式）
    std::string op;                  // 助记符
    std::vector<std::string> defs;   // 定义的寄存器
    std::vector<std::string> uses;   // 使用的寄存器
    bool isLoad = false;
    bool isStore = false;
    bool schedulable = false;        // 能否参与调度（def/use 已知且无副作用顾虑）
};

// 去除首尾空白
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 是否为寄存器名（x0-x31 / 别名 / 浮点）。保守：只认已知前缀。
bool isReg(const std::string& t) {
    if (t.empty()) return false;
    static const std::unordered_set<std::string> regs = {
        "zero","ra","sp","gp","tp","fp",
        "t0","t1","t2","t3","t4","t5","t6",
        "s0","s1","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11",
        "a0","a1","a2","a3","a4","a5","a6","a7",
        "ft0","ft1","ft2","ft3","ft4","ft5","ft6","ft7","ft8","ft9","ft10","ft11",
        "fs0","fs1","fs2","fs3","fs4","fs5","fs6","fs7","fs8","fs9","fs10","fs11",
        "fa0","fa1","fa2","fa3","fa4","fa5","fa6","fa7"
    };
    return regs.count(t) > 0;
}

// 从操作数串（逗号分隔，可能含 offset(base) 形式）提取寄存器 token。
// 返回 (纯寄存器列表, 括号内 base 寄存器列表)。
void extractRegs(const std::string& operands,
                 std::vector<std::string>& plain,
                 std::vector<std::string>& mem) {
    std::string cur;
    bool inParen = false;
    std::string parenReg;
    for (size_t i = 0; i <= operands.size(); ++i) {
        char c = (i < operands.size()) ? operands[i] : ',';
        if (c == '(') {
            cur = trim(cur);
            inParen = true; parenReg.clear();
        } else if (c == ')') {
            parenReg = trim(parenReg);
            if (isReg(parenReg)) mem.push_back(parenReg);
            inParen = false;
        } else if (c == ',' && !inParen) {
            std::string t = trim(cur);
            if (isReg(t)) plain.push_back(t);
            cur.clear();
        } else {
            if (inParen) parenReg += c; else cur += c;
        }
    }
}

// 解析一条指令行的 def/use。未知助记符 → schedulable=false。
AsmInst parseInst(const std::string& line) {
    AsmInst ai;
    ai.raw = line;
    std::string t = trim(line);
    // 拆助记符与操作数
    size_t sp = t.find_first_of(" \t");
    std::string op = (sp == std::string::npos) ? t : t.substr(0, sp);
    std::string operands = (sp == std::string::npos) ? "" : trim(t.substr(sp));
    ai.op = op;

    std::vector<std::string> plain, mem;
    extractRegs(operands, plain, mem);

    // 按助记符分类建 def/use。第一个 plain 通常是 rd。
    // load: rd, offset(base) → def=plain[0], use=mem
    static const std::unordered_set<std::string> loads = {
        "lw","ld","lh","lb","lhu","lbu","lwu","flw","fld"};
    static const std::unordered_set<std::string> stores = {
        "sw","sd","sh","sb","fsw","fsd"};
    // ALU 三址寄存器：def=plain[0], use=plain[1..]
    static const std::unordered_set<std::string> alu3 = {
        "add","sub","mul","mulw","mulh","and","or","xor","sll","srl","sra",
        "addw","subw","sllw","srlw","sraw","div","divw","rem","remw","divu","remu",
        "slt","sltu","smulh"};
    // ALU 立即/单源：def=plain[0], use=plain[1](若有)
    static const std::unordered_set<std::string> alu2 = {
        "addi","addiw","slli","srli","srai","slliw","srliw","sraiw",
        "andi","ori","xori","slti","sltiu","mv","neg","negw","not",
        "seqz","snez","sext.w","zext.w","sextw"};
    // 浮点三址/二址
    static const std::unordered_set<std::string> falu3 = {
        "fadd.s","fsub.s","fmul.s","fdiv.s","fadd.d","fsub.d","fmul.d","fdiv.d"};
    static const std::unordered_set<std::string> falu2 = {
        "fmv.s","fmv.w.x","fmv.x.w","fneg.s","fabs.s","fcvt.w.s","fcvt.s.w",
        "fsqrt.s"};
    // 常量装载：def=plain[0]，无寄存器 use
    static const std::unordered_set<std::string> loadimm = {"li","la","lui"};

    if (loads.count(op)) {
        ai.isLoad = true;
        if (!plain.empty()) ai.defs.push_back(plain[0]);
        for (auto& r : mem) ai.uses.push_back(r);
        ai.schedulable = !ai.defs.empty();
    } else if (stores.count(op)) {
        ai.isStore = true;
        // store rs, offset(base): rs 是 use，base 是 use，无 def
        for (auto& r : plain) ai.uses.push_back(r);
        for (auto& r : mem) ai.uses.push_back(r);
        ai.schedulable = true;
    } else if (alu3.count(op) || falu3.count(op)) {
        if (!plain.empty()) {
            ai.defs.push_back(plain[0]);
            for (size_t i = 1; i < plain.size(); ++i) ai.uses.push_back(plain[i]);
        }
        for (auto& r : mem) ai.uses.push_back(r);
        ai.schedulable = !ai.defs.empty();
    } else if (alu2.count(op) || falu2.count(op)) {
        if (!plain.empty()) {
            ai.defs.push_back(plain[0]);
            for (size_t i = 1; i < plain.size(); ++i) ai.uses.push_back(plain[i]);
        }
        for (auto& r : mem) ai.uses.push_back(r);
        ai.schedulable = !ai.defs.empty();
    } else if (loadimm.count(op)) {
        if (!plain.empty()) ai.defs.push_back(plain[0]);
        ai.schedulable = !ai.defs.empty();
    } else {
        // 未识别（含 call/j/b*/ret/fcmp 等）→ 不可调度，作硬边界。
        ai.schedulable = false;
    }

    // 安全：任何涉及 sp/ra/gp/tp 的指令不参与调度（栈帧/调用约定敏感）。
    for (auto& r : ai.defs)
        if (r == "sp" || r == "ra" || r == "gp" || r == "tp") ai.schedulable = false;
    for (auto& r : ai.uses)
        if (r == "sp" || r == "ra" || r == "gp" || r == "tp") ai.schedulable = false;

    return ai;
}

int latencyOf(const AsmInst& a) {
    if (a.isLoad) return 3;
    if (a.op == "mul" || a.op == "mulw" || a.op == "mulh" || a.op == "smulh") return 3;
    if (a.op.rfind("div", 0) == 0 || a.op.rfind("rem", 0) == 0 ||
        a.op == "divw" || a.op == "remw") return 5;
    if (a.op.rfind("fdiv", 0) == 0 || a.op.rfind("fmul", 0) == 0) return 4;
    return 1;
}

// 对一个"可调度块"（全部 schedulable 的连续指令）做延迟感知列表调度。
// 返回新顺序的下标；若不变返回空表示无需重排。
std::vector<int> scheduleBlock(const std::vector<AsmInst>& blk) {
    const int n = (int)blk.size();
    std::vector<std::vector<int>> succ(n);
    std::vector<int> indeg(n, 0);
    auto addEdge = [&](int u, int v) { if (u != v) { succ[u].push_back(v); indeg[v]++; } };

    // 最近写某寄存器的指令下标；某寄存器最近被读的指令集合（WAR 用）。
    std::unordered_map<std::string, int> lastDef;
    std::unordered_map<std::string, std::vector<int>> readers;
    int lastStore = -1;
    std::vector<int> loadsSinceStore;

    for (int i = 0; i < n; ++i) {
        const auto& a = blk[i];
        // RAW: use 依赖最近的 def
        for (auto& u : a.uses) {
            auto it = lastDef.find(u);
            if (it != lastDef.end()) addEdge(it->second, i);
        }
        // WAR: def 依赖此前所有 reader；WAW: def 依赖上一个 def
        for (auto& d : a.defs) {
            auto rit = readers.find(d);
            if (rit != readers.end()) for (int r : rit->second) addEdge(r, i);
            auto dit = lastDef.find(d);
            if (dit != lastDef.end()) addEdge(dit->second, i);
        }
        // 内存依赖
        if (a.isLoad) {
            if (lastStore >= 0) addEdge(lastStore, i);
            loadsSinceStore.push_back(i);
        } else if (a.isStore) {
            if (lastStore >= 0) addEdge(lastStore, i);
            for (int ld : loadsSinceStore) addEdge(ld, i);
            lastStore = i;
            loadsSinceStore.clear();
        }
        // 更新状态
        for (auto& d : a.defs) { lastDef[d] = i; readers[d].clear(); }
        for (auto& u : a.uses) readers[u].push_back(i);
    }

    // 关键路径高度
    std::vector<int> height(n, -1);
    std::function<int(int)> H = [&](int u) -> int {
        if (height[u] >= 0) return height[u];
        int h = 0;
        for (int w : succ[u]) h = std::max(h, H(w));
        return height[u] = latencyOf(blk[u]) + h;
    };
    for (int i = 0; i < n; ++i) H(i);

    // 列表调度
    std::vector<int> deg = indeg, ready, out;
    for (int i = 0; i < n; ++i) if (deg[i] == 0) ready.push_back(i);
    out.reserve(n);
    while (!ready.empty()) {
        int best = 0;
        for (int i = 1; i < (int)ready.size(); ++i) {
            int a = ready[i], b = ready[best];
            if (height[a] > height[b] || (height[a] == height[b] && a < b)) best = i;
        }
        int u = ready[best];
        ready.erase(ready.begin() + best);
        out.push_back(u);
        for (int w : succ[u]) if (--deg[w] == 0) ready.push_back(w);
    }
    if ((int)out.size() != n) return {};   // 兜底
    return out;
}

} // namespace

std::string postRASchedule(const std::string& asmText) {
    if (const char* v = std::getenv("SCHED_OFF"))
        if (std::string(v) == "1") return asmText;

    std::istringstream in(asmText);
    std::vector<std::string> lines;
    std::string ln;
    while (std::getline(in, ln)) lines.push_back(ln);

    std::ostringstream out;

    // 是否为不可调度的边界行（标签/指示符/空行/注释）。
    auto isBoundaryLine = [](const std::string& s) -> bool {
        std::string t = trim(s);
        if (t.empty()) return true;
        if (t[0] == '.') return true;          // 指示符 或 .L 标签
        if (t.back() == ':') return true;       // 标签
        if (t[0] == '#') return true;           // 注释
        return false;
    };

    size_t i = 0;
    while (i < lines.size()) {
        if (isBoundaryLine(lines[i])) { out << lines[i] << "\n"; ++i; continue; }

        // 收集一段连续指令行，解析为 AsmInst。
        std::vector<AsmInst> blk;
        size_t start = i;
        while (i < lines.size() && !isBoundaryLine(lines[i])) {
            blk.push_back(parseInst(lines[i]));
            ++i;
        }

        // 若整段全部 schedulable，调度；否则原样输出（安全）。
        bool allSched = true;
        for (auto& a : blk) if (!a.schedulable) { allSched = false; break; }

        if (allSched && blk.size() > 1) {
            auto order = scheduleBlock(blk);
            if (!order.empty()) {
                for (int idx : order) out << blk[idx].raw << "\n";
                continue;
            }
        }
        // 未调度：原样输出
        for (size_t k = start; k < i; ++k) out << lines[k] << "\n";
    }

    return out.str();
}

} // namespace Backend
