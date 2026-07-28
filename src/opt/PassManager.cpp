// ================================================================
// 参数化 Pass 开关（借鉴 Cpl8 的 --test= 机制，改用环境变量）
//
// 用法：
//   OPT_DISABLE="globalValueNumbering,loopInvariantCodeMotion"
//       黑名单：跳过列出的 pass（逗号/空格/分号分隔）
//   OPT_ENABLE="globalValueNumbering,sparseConditionalConstantPropagation"
//       白名单：只运行列出的 pass（其余全部跳过）
//   白名单非空时优先于黑名单。
//
// 键名 = Optimizer.h 中的 pass 函数名（如 mem2reg / simplifyCFG / GVN 别名
// globalValueNumbering）。兼容旧开关 OPT_DISABLE_GVN=1。
//
// 注意：constantFolding / deadCodeElimination 是清理性 pass，不参与开关，
//       始终运行以保证 IR 整洁。
// ================================================================

#include "opt/Optimizer.h"
#include <cstdlib>
#include <string>
#include <unordered_set>

namespace Opt {
namespace {

std::unordered_set<std::string> parseEnvList(const char* var) {
    std::unordered_set<std::string> out;
    const char* v = std::getenv(var);
    if (!v) return out;
    std::string cur;
    for (const char* p = v; ; ++p) {
        char c = *p;
        if (c == '\0' || c == ',' || c == ' ' || c == ';' || c == '\t') {
            if (!cur.empty()) {
                // 常用别名归一化
                if (cur == "GVN") cur = "globalValueNumbering";
                if (cur == "SCCP") cur = "sparseConditionalConstantPropagation";
                if (cur == "LICM") cur = "loopInvariantCodeMotion";
                if (cur == "CSE") cur = "commonSubexpressionElimination";
                if (cur == "DSE") cur = "deadStoreElimination";
                out.insert(cur);
                cur.clear();
            }
            if (c == '\0') break;
        } else {
            cur.push_back(c);
        }
    }
    return out;
}

} // namespace

bool passEnabled(const std::string& name) {
    static const std::unordered_set<std::string> enableList = parseEnvList("OPT_ENABLE");
    static const std::unordered_set<std::string> disableList = parseEnvList("OPT_DISABLE");

    if (!enableList.empty())
        return enableList.count(name) > 0;
    if (disableList.count(name) > 0)
        return false;

    // ★ 内置默认禁用列表：这些 pass 在 BOOM 目标上无收益或有 bug。
    //   可用 OPT_ENABLE=passName 强制开启（用于调试/对比）。
    //   - loopStrengthReduce：BOOM mul 单周期全流水，强度削减净负收益（A3/B2 验证）；
    //     且在多 BB 循环体（如 crypto 的 3*i）上累加器变换有 bug，破坏计算。
    static const std::unordered_set<std::string> builtinDisable = {
        "loopStrengthReduce",
    };
    if (builtinDisable.count(name) > 0)
        return false;

    // 兼容旧开关 OPT_DISABLE_GVN=1
    if (name == "globalValueNumbering") {
        static const bool gvnOff = [] {
            const char* v = std::getenv("OPT_DISABLE_GVN");
            return v && std::string(v) == "1";
        }();
        if (gvnOff) return false;
    }
    return true;
}

} // namespace Opt
