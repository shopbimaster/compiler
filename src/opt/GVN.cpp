// ================================================================
// GVN — 全局值编号（Global Value Numbering）
// 基于支配树的跨 BB 公共子表达式消除
//
// 算法：
//   1. 计算支配树
//   2. DFS 遍历支配树，维护"可用表达式"哈希表
//   3. 对纯指令（GEP、算术等），检查是否有相同表达式可用
//   4. 如果有，替换所有使用并删除冗余指令
//
// 安全性：
//   - GEP 是纯函数（无副作用），可安全 CSE
//   - 只处理支配关系明确的指令（dominating → dominated）
//   - 不处理 LOAD（需要别名分析）
//
// 寄存器压力控制：
//   - CSE 合并 GEP 会延长活跃区间，可能增加寄存器溢出
//   - 通过限制：只 CSE 被同一 BB 或直接支配 BB 中的 GEP
//   - 以及在循环热路径上限制 CSE 数量
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// ---- GVN 键：将 (opcode, all operands, 可选 condition) 哈希 ----
struct GVNKey {
    IR::Instruction::Opcode opcode;
    std::vector<IR::Value*> ops;
    std::string condition; // 仅 ICMP/FCMP 用于区分 eq/ne/slt/sle/sgt/sge

    bool operator==(const GVNKey& other) const {
        return opcode == other.opcode &&
               ops == other.ops &&
               condition == other.condition;
    }
};

struct GVNKeyHash {
    std::size_t operator()(const GVNKey& k) const {
        auto h = std::hash<uintptr_t>{};
        std::size_t seed = static_cast<std::size_t>(k.opcode);
        for (auto* v : k.ops) {
            seed ^= h(reinterpret_cast<uintptr_t>(v)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        if (!k.condition.empty())
            seed ^= std::hash<std::string>{}(k.condition) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// ---- 判断指令是否适合 GVN ----
// 扩展：除GEP外，也对纯算术运算做GVN
// 理由：
//   1. 图着色寄存器分配器比线性扫描更能处理活跃区间延长
//   2. 算术运算GVN可以消除跨BB的冗余计算（如循环中的索引计算）
//   3. 只处理纯函数式指令，确保安全性
bool canGVN(IR::Instruction* inst) {
    auto op = inst->getOpcode();

    // GEP — 跨 BB 冗余的主要来源
    if (op == Opc::GETELEMENTPTR) return true;

    // 纯整数算术运算 — 无副作用，可以安全GVN
    // 不包括浮点运算（需要考虑浮点异常和舍入模式）
    if (op == Opc::ADD || op == Opc::SUB || op == Opc::MUL ||
        op == Opc::SDIV || op == Opc::SREM ||
        op == Opc::AND || op == Opc::OR || op == Opc::XOR ||
        op == Opc::SHL || op == Opc::ASHR) {
        return true;
    }

    // 整数比较 — 纯函数，可以GVN
    if (op == Opc::ICMP) return true;

    return false;
}

// ---- 构造 GVN 键 ----
GVNKey makeGVNKey(IR::Instruction* inst) {
    GVNKey key;
    key.opcode = inst->getOpcode();
    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
        key.ops.push_back(inst->getOperand(i));
    }
    if (inst->getOpcode() == Opc::ICMP || inst->getOpcode() == Opc::FCMP) {
        key.condition = inst->getName();
    }
    return key;
}

// ---- 构建支配树子节点映射 ----
std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>> buildDomTreeChildren(
    IR::Function* func,
    const std::unordered_map<IR::BasicBlock*, IR::BasicBlock*>& idom) {
    std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>> children;
    for (auto& bb : func->getBlocks()) {
        children[bb.get()];
    }
    for (auto& [bb, parent] : idom) {
        if (parent) {
            children[parent].push_back(bb);
        }
    }
    return children;
}

// ---- 单函数 GVN ----
bool gvnOnFunction(IR::Function* func) {
    if (func->isExternal() || func->getBlocks().empty())
        return false;

    auto dom = computeDominators(func);
    auto idom = computeImmediateDominators(func, dom);
    auto children = buildDomTreeChildren(func, idom);

    // 用 scoped hash table 实现支配树 DFS
    // 每个 BB 继承 idom 的可用表达式，添加自己的表达式
    // 退出时恢复（通过记录添加的数量）
    std::unordered_map<GVNKey, IR::Instruction*, GVNKeyHash> available;

    std::vector<IR::Instruction*> toErase;
    bool changed = false;

    // DFS 遍历支配树
    std::function<void(IR::BasicBlock*)> dfs = [&](IR::BasicBlock* bb) {
        // 记录当前可用表达式数量，用于退出时恢复
        size_t savedSize = available.size();
        std::vector<GVNKey> addedKeys;

        // 当前 BB 的直接支配者
        auto idomIt = idom.find(bb);
        IR::BasicBlock* bbIdom = (idomIt != idom.end()) ? idomIt->second : nullptr;

        for (auto it = bb->begin(); it != bb->end(); ) {
            auto* inst = it->get();

            if (!canGVN(inst)) {
                ++it;
                continue;
            }

            auto key = makeGVNKey(inst);
            auto found = available.find(key);

            if (found != available.end() && found->second != inst) {
                // ★ 安全检查：只合并来自直接支配者的 GEP
                // 原因：寄存器分配器对跨多 BB 的长活跃区间处理有 bug。
                // 限制为直接支配者（idom）可将活跃区间限制在 1 个 BB 边界内。
                // 来自更远祖先的 GEP 不合并，避免触发寄存器分配器 bug。
                auto* existingBB = found->second->getParent();
                if (existingBB != bb && existingBB != bbIdom) {
                    // 不是同 BB 也不是直接支配者 — 跳过合并
                    // 但将当前 GEP 注册为新可用值，供直接子节点使用
                    bool alreadyAdded = false;
                    for (auto& ak : addedKeys) {
                        if (ak == key) { alreadyAdded = true; break; }
                    }
                    if (!alreadyAdded) {
                        available[key] = inst;
                        addedKeys.push_back(key);
                    }
                    ++it;
                    continue;
                }
                // 同 BB 或直接支配者 — 安全合并
                inst->replaceAllUsesWith(found->second);
                inst->dropAllUses();
                it = bb->erase(it);
                changed = true;
            } else {
                // 新表达式 — 添加到可用集合
                if (found == available.end()) {
                    available[key] = inst;
                    addedKeys.push_back(key);
                }
                ++it;
            }
        }

        // 递归处理支配树子节点
        auto childIt = children.find(bb);
        if (childIt != children.end()) {
            for (auto* child : childIt->second) {
                dfs(child);
            }
        }

        // 恢复可用表达式表：移除本 BB 添加的条目
        for (const auto& key : addedKeys) {
            available.erase(key);
        }
        (void)savedSize;
    };

    // 从 entry 开始 DFS
    dfs(func->getEntryBlock());

    return changed;
}

} // namespace

bool globalValueNumbering(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (gvnOnFunction(func.get())) {
            changed = true;
        }
    }
    return changed;
}

} // namespace Opt
