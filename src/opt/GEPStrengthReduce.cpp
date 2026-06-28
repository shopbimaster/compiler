// ================================================================
// GEP 强度削弱（GEP Strength Reduction）
// 借鉴 Cpl3 的 GEPStrengthReduce 设计
// 将循环中的 GETELEMENTPTR 地址计算替换为累加地址
// 对于数组访问 a[i]，每次迭代地址 += sizeof(element)
// 消除循环内的乘法运算
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// ================================================================
// 分析 GETELEMENTPTR 的模式：
// GETELEMENTPTR base, offset
// 如果 offset 是 MUL(i, elemSize) 或直接用 i 做索引
// 其中 i 是归纳变量，则将其替换为累加地址
// ================================================================
bool reduceGEPInLoop(const NaturalLoop& loop, IR::Function* func) {
    auto info = analyzeLoopInduction(loop, func);
    if (info.tripCount < 2 || !info.var) return false;

    bool changed = false;

    // 遍历循环体中的所有 GETELEMENTPTR 指令
    for (auto* bb : loop.body) {
        for (auto& inst : bb->getInstructions()) {
            // 跳过已经处理过的指令（可能已被替换）
            if (inst->getOpcode() != IR::Instruction::Opcode::GETELEMENTPTR) continue;

            // GETELEMENTPTR: base[offset]
            // 检查 offset 是否涉及归纳变量
            auto* offset = inst->getOperand(1);  // GETELEMENTPTR: operands[0]=base, operands[1]=offset
            if (!offset) continue;

            bool involvesInduction = false;
            IR::Value* stepSize = nullptr;

            // 检查 offset 是否是 MUL(LOAD(iv), const)
            if (auto* offsetInst = dynamic_cast<IR::Instruction*>(offset)) {
                if (offsetInst->getOpcode() == IR::Instruction::Opcode::MUL) {
                    auto* mulOp0 = offsetInst->getOperand(0);
                    auto* mulOp1 = offsetInst->getOperand(1);

                    // 检查 mulOp0 是否使用归纳变量
                    if (auto* op0Inst = dynamic_cast<IR::Instruction*>(mulOp0)) {
                        if (op0Inst->getOpcode() == IR::Instruction::Opcode::LOAD &&
                            op0Inst->getOperand(0) == info.var) {
                            involvesInduction = true;
                            stepSize = mulOp1;  // 常量步长
                        }
                    }
                    if (!involvesInduction && dynamic_cast<IR::Instruction*>(mulOp1)) {
                        auto* op1Inst = dynamic_cast<IR::Instruction*>(mulOp1);
                        if (op1Inst->getOpcode() == IR::Instruction::Opcode::LOAD &&
                            op1Inst->getOperand(0) == info.var) {
                            involvesInduction = true;
                            stepSize = mulOp0;  // 常量步长
                        }
                    }
                }
            }

            if (!involvesInduction) continue;

            // 有归纳变量参与的 GEP：创建累加地址
            auto* base = inst->getOperand(0);
            auto* stepCI = dynamic_cast<IR::ConstantInt*>(stepSize);
            if (!stepCI) continue;

            int64_t elemSize = stepCI->getValue();
            int64_t ivStep = 0;
            if (auto* s = dynamic_cast<IR::ConstantInt*>(info.step)) {
                ivStep = s->getValue();
            }
            int64_t addrStep = elemSize * ivStep;

            // 创建累加地址变量
            auto* addrAlloca = IR::Instruction::createAlloca(
                IR::PointerType::get(IR::IntegerType::get(8)), "gep.lsr.ptr");

            // 插入到 entry block
            auto* entry = func->getEntryBlock();
            auto entryIt = entry->begin();
            while (entryIt != entry->end() && (*entryIt)->getOpcode() == IR::Instruction::Opcode::ALLOCA) {
                ++entryIt;
            }
            entry->insert(entryIt, addrAlloca);

            // 初始化地址 = base
            auto* initStore = IR::Instruction::createStore(base, addrAlloca);
            entry->insert(entryIt, initStore);

            // 在循环头中 LOAD 地址
            auto* loadAddr = IR::Instruction::createLoad(
                IR::PointerType::get(IR::IntegerType::get(8)), addrAlloca, "gep.lsr.load");
            auto headerIt = loop.header->begin();
            while (headerIt != loop.header->end() && (*headerIt)->getOpcode() == IR::Instruction::Opcode::ALLOCA) {
                ++headerIt;
            }
            loop.header->insert(headerIt, loadAddr);

            // 替换 GEP 的所有 uses
            inst->replaceAllUsesWith(loadAddr);

            // 在循环体末尾累加地址
            auto* stepConst = IR::ConstantInt::get(IR::IntegerType::get(32), addrStep);
            // 需要将 stepConst 转换为指针类型... 实际上 GEP 用 i8* 做指针运算
            // 用 GETELEMENTPTR 来累加地址
            auto* gepInc = IR::Instruction::createGetElementPtr(
                IR::PointerType::get(IR::IntegerType::get(8)),
                loadAddr, {stepConst}, "gep.lsr.inc");

            // 找到 STORE 归纳变量之后插入
            for (auto* b : loop.body) {
                for (auto it = b->begin(); it != b->end(); ++it) {
                    if ((*it)->getOpcode() == IR::Instruction::Opcode::STORE) {
                        if ((*it)->getOperand(1) == info.var) {
                            ++it;
                            b->insert(it, gepInc);
                            auto* storeAddr = IR::Instruction::createStore(gepInc, addrAlloca);
                            b->insert(it, storeAddr);
                            changed = true;
                            break;
                        }
                    }
                }
                if (changed) break;
            }

            if (changed) break;  // 一次只处理一个
        }
        if (changed) break;
    }

    return changed;
}

} // namespace

bool gepStrengthReduce(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        auto loops = findNaturalLoops(func.get());
        for (auto& loop : loops) {
            if (reduceGEPInLoop(loop, func.get())) changed = true;
        }
    }
    return changed;
}

} // namespace Opt