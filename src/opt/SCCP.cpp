// ================================================================
// 稀疏条件常量传播（Sparse Conditional Constant Propagation）
// 借鉴 Cpl3 的 SCCP 设计
// 核心思想：结合常量传播与控制流分析，在常量传播的同时确定哪些
//          分支是可达的，从而实现更精确的常量传播和死代码消除
// 算法：使用 lattice（⊤→常量→⊥）和 worklist，迭代直到收敛
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <cassert>

namespace Opt {
namespace {

enum class LatticeState {
    TOP,        // ⊤: 未知（尚未计算）
    CONSTANT,   // 常量
    BOTTOM      // ⊥: 不是常量（overdefined）
};

struct LatticeValue {
    LatticeState state = LatticeState::TOP;
    IR::ConstantInt* constant = nullptr;  // 仅当 state == CONSTANT 时有效
};

// ================================================================
// 计算二元运算的常量结果
// ================================================================
IR::ConstantInt* foldBinOp(IR::Instruction::Opcode op, int64_t lhs, int64_t rhs) {
    using Opc = IR::Instruction::Opcode;
    int64_t result = 0;
    switch (op) {
        case Opc::ADD:  result = lhs + rhs; break;
        case Opc::SUB:  result = lhs - rhs; break;
        case Opc::MUL:  result = lhs * rhs; break;
        case Opc::SDIV:
            if (rhs == 0) return nullptr;
            result = lhs / rhs; break;
        case Opc::SREM:
            if (rhs == 0) return nullptr;
            result = lhs % rhs; break;
        case Opc::AND:  result = lhs & rhs; break;
        case Opc::OR:   result = lhs | rhs; break;
        case Opc::XOR:  result = lhs ^ rhs; break;
        case Opc::SHL:  result = lhs << rhs; break;
        case Opc::ASHR: result = lhs >> rhs; break;
        default: return nullptr;
    }
    return IR::ConstantInt::get(IR::IntegerType::get(32), result);
}

// ================================================================
// 计算比较运算的常量结果
// ================================================================
IR::ConstantInt* foldCmp(const std::string& cond, int64_t lhs, int64_t rhs) {
    bool result = false;
    if (cond == "eq")  result = (lhs == rhs);
    else if (cond == "ne") result = (lhs != rhs);
    else if (cond == "slt") result = (lhs < rhs);
    else if (cond == "sle") result = (lhs <= rhs);
    else if (cond == "sgt") result = (lhs > rhs);
    else if (cond == "sge") result = (lhs >= rhs);
    else return nullptr;
    return IR::ConstantInt::get(IR::IntegerType::get(1), result ? 1 : 0);
}

// ================================================================
// 计算类型转换的常量结果
// ================================================================
IR::ConstantInt* foldCast(IR::Instruction::Opcode op, int64_t src) {
    using Opc = IR::Instruction::Opcode;
    int64_t result = 0;
    switch (op) {
        case Opc::ZEXT:
            // 零扩展：保持低32位
            result = static_cast<uint32_t>(src);
            break;
        case Opc::SEXT:
            // 符号扩展（当前均为 i32）
            result = src;
            break;
        case Opc::TRUNC:
            // 截断（当前均为 i32）
            result = src;
            break;
        default: return nullptr;
    }
    return IR::ConstantInt::get(IR::IntegerType::get(32), result);
}

// ================================================================
// 计算指令的结果 lattice 值
// ================================================================
LatticeValue evaluateInst(
    IR::Instruction* inst,
    const std::unordered_map<IR::Value*, LatticeValue>& lattice) {
    using Opc = IR::Instruction::Opcode;
    auto op = inst->getOpcode();
    LatticeValue result;

    // 获取操作数的 lattice 值
    auto getLattice = [&](unsigned i) -> LatticeValue {
        auto* val = inst->getOperand(i);
        if (!val) return {LatticeState::BOTTOM, nullptr};
        auto it = lattice.find(val);
        if (it == lattice.end()) return {LatticeState::BOTTOM, nullptr};
        return it->second;
    };

    // 对于二元运算：需要两个操作数都是常量
    if (op == Opc::ADD || op == Opc::SUB || op == Opc::MUL || op == Opc::SDIV ||
        op == Opc::SREM || op == Opc::AND || op == Opc::OR  || op == Opc::XOR ||
        op == Opc::SHL || op == Opc::ASHR) {
        auto lv0 = getLattice(0);
        auto lv1 = getLattice(1);
        if (lv0.state == LatticeState::BOTTOM || lv1.state == LatticeState::BOTTOM) {
            result.state = LatticeState::BOTTOM;
        } else if (lv0.state == LatticeState::CONSTANT && lv1.state == LatticeState::CONSTANT) {
            auto* folded = foldBinOp(op, lv0.constant->getValue(), lv1.constant->getValue());
            if (folded) {
                result.state = LatticeState::CONSTANT;
                result.constant = folded;
            } else {
                result.state = LatticeState::BOTTOM;
            }
        }
        // else: one or both are TOP → result stays TOP
        return result;
    }

    // ICMP/FCMP
    if (op == Opc::ICMP) {
        auto lv0 = getLattice(0);
        auto lv1 = getLattice(1);
        if (lv0.state == LatticeState::BOTTOM || lv1.state == LatticeState::BOTTOM) {
            result.state = LatticeState::BOTTOM;
        } else if (lv0.state == LatticeState::CONSTANT && lv1.state == LatticeState::CONSTANT) {
            auto* folded = foldCmp(inst->getName(), lv0.constant->getValue(), lv1.constant->getValue());
            if (folded) {
                result.state = LatticeState::CONSTANT;
                result.constant = folded;
            } else {
                result.state = LatticeState::BOTTOM;
            }
        }
        return result;
    }

    // 类型转换
    if (op == Opc::ZEXT || op == Opc::SEXT || op == Opc::TRUNC) {
        auto lv = getLattice(0);
        if (lv.state == LatticeState::BOTTOM) {
            result.state = LatticeState::BOTTOM;
        } else if (lv.state == LatticeState::CONSTANT) {
            auto* folded = foldCast(op, lv.constant->getValue());
            if (folded) {
                result.state = LatticeState::CONSTANT;
                result.constant = folded;
            } else {
                result.state = LatticeState::BOTTOM;
            }
        }
        return result;
    }

    // SELECT: 如果条件和两个值都是常量
    if (op == Opc::SELECT) {
        auto lvCond = getLattice(0);
        if (lvCond.state == LatticeState::CONSTANT && lvCond.constant) {
            bool takeTrue = (lvCond.constant->getValue() != 0);
            auto lv = getLattice(takeTrue ? 1 : 2);
            return lv;
        }
        // 如果两个值相同（即使条件未知），结果也是那个值
        auto lv1 = getLattice(1);
        auto lv2 = getLattice(2);
        if (lv1.state == LatticeState::CONSTANT && lv2.state == LatticeState::CONSTANT &&
            lv1.constant->getValue() == lv2.constant->getValue()) {
            result.state = LatticeState::CONSTANT;
            result.constant = lv1.constant;
            return result;
        }
        if (lv1.state == LatticeState::BOTTOM || lv2.state == LatticeState::BOTTOM) {
            result.state = LatticeState::BOTTOM;
        }
        return result;
    }

    // LOAD, STORE, CALL, ALLOCA, GETELEMENTPTR, BR, COND_BR, RET, PHI
    // 这些指令的结果不是常量
    result.state = LatticeState::BOTTOM;
    return result;
}

// ================================================================
// 单函数 SCCP
// ================================================================
bool sccpOnFunction(IR::Function* func) {
    auto& blocks = func->getBlocks();
    if (blocks.empty()) return false;

    // Lattice: 每个 Value 的 lattice 值
    std::unordered_map<IR::Value*, LatticeValue> lattice;

    // 可执行的基本块
    std::unordered_set<IR::BasicBlock*> executable;
    std::queue<IR::BasicBlock*> bbWorklist;

    // 从入口块开始
    auto* entry = func->getEntryBlock();
    executable.insert(entry);
    bbWorklist.push(entry);

    // 已知常量值的 worklist（用于传播）
    std::queue<IR::Instruction*> ssaWorklist;

    // 初始化：所有常量（ConstantInt）初始化为 CONSTANT 状态
    for (auto& bb : blocks) {
        for (auto& inst : bb->getInstructions()) {
            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                auto* val = inst->getOperand(i);
                if (auto* ci = dynamic_cast<IR::ConstantInt*>(val)) {
                    if (lattice.find(val) == lattice.end()) {
                        lattice[val] = {LatticeState::CONSTANT, ci};
                    }
                }
            }
        }
    }

    // 主循环：处理可执行的基本块
    while (!bbWorklist.empty() || !ssaWorklist.empty()) {
        // 先处理 SSA worklist（传播常量值）
        while (!ssaWorklist.empty()) {
            auto* inst = ssaWorklist.front();
            ssaWorklist.pop();

            auto newLattice = evaluateInst(inst, lattice);
            auto& oldLattice = lattice[inst];

            // 如果 lattice 值发生变化
            if (oldLattice.state != newLattice.state ||
                (newLattice.state == LatticeState::CONSTANT &&
                 oldLattice.constant != newLattice.constant)) {
                oldLattice = newLattice;

                // 将使用该值的所有指令加入 SSA worklist
                for (auto& use : inst->getUses()) {
                    if (auto* userInst = dynamic_cast<IR::Instruction*>(use.user)) {
                        ssaWorklist.push(userInst);
                    }
                }

                // 如果这是一个 COND_BR 的条件，且变为常量，可能影响可执行块
                if (inst->getOpcode() == IR::Instruction::Opcode::ICMP ||
                    inst->getOpcode() == IR::Instruction::Opcode::FCMP) {
                    // 检查是否有 COND_BR 使用这个比较结果
                    for (auto& use : inst->getUses()) {
                        if (auto* userInst = dynamic_cast<IR::Instruction*>(use.user)) {
                            if (userInst->getOpcode() == IR::Instruction::Opcode::COND_BR) {
                                auto* condBB = userInst->getParent();
                                if (executable.count(condBB)) {
                                    bbWorklist.push(condBB);
                                }
                            }
                        }
                    }
                }
            }
        }

        // 处理 BB worklist
        if (bbWorklist.empty()) break;
        auto* bb = bbWorklist.front();
        bbWorklist.pop();

        // 处理 BB 中的所有指令
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();

            // 特殊处理 COND_BR：根据条件决定哪些后继可执行
            if (op == IR::Instruction::Opcode::COND_BR) {
                auto* cond = inst->getOperand(0);
                auto* thenBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(1));
                auto* elseBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(2));

                auto it = lattice.find(cond);
                bool condKnown = (it != lattice.end() && it->second.state == LatticeState::CONSTANT);

                if (condKnown && it->second.constant) {
                    bool takeTrue = (it->second.constant->getValue() != 0);
                    auto* target = takeTrue ? thenBB : elseBB;
                    if (target && !executable.count(target)) {
                        executable.insert(target);
                        bbWorklist.push(target);
                    }
                } else {
                    // 条件未知或不是常量：两个分支都可执行
                    if (thenBB && !executable.count(thenBB)) {
                        executable.insert(thenBB);
                        bbWorklist.push(thenBB);
                    }
                    if (elseBB && !executable.count(elseBB)) {
                        executable.insert(elseBB);
                        bbWorklist.push(elseBB);
                    }
                }
                continue;  // COND_BR 本身不产生值
            }

            // 特殊处理 BR：无条件跳转
            if (op == IR::Instruction::Opcode::BR) {
                auto* target = dynamic_cast<IR::BasicBlock*>(inst->getOperand(0));
                if (target && !executable.count(target)) {
                    executable.insert(target);
                    bbWorklist.push(target);
                }
                continue;
            }

            // 计算指令的 lattice 值
            auto newLattice = evaluateInst(inst.get(), lattice);
            auto& oldLattice = lattice[inst.get()];

            if (oldLattice.state != newLattice.state ||
                (newLattice.state == LatticeState::CONSTANT &&
                 oldLattice.constant != newLattice.constant)) {
                oldLattice = newLattice;

                // 传播到 uses
                for (auto& use : inst->getUses()) {
                    if (auto* userInst = dynamic_cast<IR::Instruction*>(use.user)) {
                        ssaWorklist.push(userInst);
                    }
                }
            }
        }
    }

    // ================================================================
    // 第二阶段：应用结果
    // 1. 将常量值替换为 ConstantInt
    // 2. 将不可达块的条件分支替换为无条件分支
    // 3. 删除不可达块
    // ================================================================
    bool changed = false;

    // 收集不可达块
    std::unordered_set<IR::BasicBlock*> deadBlocks;
    for (auto& bb : blocks) {
        if (!executable.count(bb.get())) {
            deadBlocks.insert(bb.get());
        }
    }

    // 替换常量值
    for (auto& bb : blocks) {
        if (deadBlocks.count(bb.get())) continue;
        for (auto& inst : bb->getInstructions()) {
            auto it = lattice.find(inst.get());
            if (it != lattice.end() && it->second.state == LatticeState::CONSTANT && it->second.constant) {
                // 跳过 COND_BR, BR, STORE, RET — 这些不能替换
                auto op = inst->getOpcode();
                if (op == IR::Instruction::Opcode::COND_BR ||
                    op == IR::Instruction::Opcode::BR ||
                    op == IR::Instruction::Opcode::STORE ||
                    op == IR::Instruction::Opcode::RET ||
                    op == IR::Instruction::Opcode::CALL) continue;

                // 替换为常量
                inst->replaceAllUsesWith(it->second.constant);
                changed = true;
            }
        }
    }

    // 将不可达块的条件分支替换为无条件分支（如果条件已知）
    for (auto& bb : blocks) {
        if (deadBlocks.count(bb.get())) continue;
        auto* term = bb->getTerminator();
        if (!term || term->getOpcode() != IR::Instruction::Opcode::COND_BR) continue;

        auto* cond = term->getOperand(0);
        auto it = lattice.find(cond);
        if (it != lattice.end() && it->second.state == LatticeState::CONSTANT && it->second.constant) {
            bool takeTrue = (it->second.constant->getValue() != 0);
            auto* target = dynamic_cast<IR::BasicBlock*>(term->getOperand(takeTrue ? 1 : 2));

            // 创建无条件 BR 替换 COND_BR
            auto* br = IR::Instruction::createBr(target);
            term->dropAllUses();
            // 找到 terminator 并替换
            for (auto it2 = bb->begin(); it2 != bb->end(); ++it2) {
                if (it2->get() == term) {
                    *it2 = std::unique_ptr<IR::Instruction>(br);
                    changed = true;
                    break;
                }
            }
        }
    }

    // 删除不可达块
    // 修复前驱中指向死块的 COND_BR 引用，然后移除死块
    if (!deadBlocks.empty()) {
        for (auto& bb : blocks) {
            if (deadBlocks.count(bb.get())) continue;
            auto* term = bb->getTerminator();
            if (!term) continue;

            if (term->getOpcode() == IR::Instruction::Opcode::COND_BR) {
                auto* thenBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
                auto* elseBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
                bool thenDead = thenBB && deadBlocks.count(thenBB);
                bool elseDead = elseBB && deadBlocks.count(elseBB);

                if (thenDead && !elseDead) {
                    // 替换为无条件 BR 到 else
                    auto* br = IR::Instruction::createBr(elseBB);
                    term->dropAllUses();
                    for (auto it2 = bb->begin(); it2 != bb->end(); ++it2) {
                        if (it2->get() == term) {
                            *it2 = std::unique_ptr<IR::Instruction>(br);
                            changed = true;
                            break;
                        }
                    }
                } else if (!thenDead && elseDead) {
                    // 替换为无条件 BR 到 then
                    auto* br = IR::Instruction::createBr(thenBB);
                    term->dropAllUses();
                    for (auto it2 = bb->begin(); it2 != bb->end(); ++it2) {
                        if (it2->get() == term) {
                            *it2 = std::unique_ptr<IR::Instruction>(br);
                            changed = true;
                            break;
                        }
                    }
                }
                // 两个都死：不应该发生（此块本身也应是死的）
            }
        }

        // 移除死块
        for (auto* deadBB : deadBlocks) {
            for (auto& inst : deadBB->getInstructions()) {
                inst->dropAllUses();
            }
            auto& blks = func->getBlocks();
            for (auto it = blks.begin(); it != blks.end(); ++it) {
                if (it->get() == deadBB) {
                    blks.erase(it);
                    break;
                }
            }
        }
    }

    return changed;
}

} // namespace

bool sparseConditionalConstantPropagation(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        if (sccpOnFunction(func.get())) changed = true;
    }
    return changed;
}

} // namespace Opt