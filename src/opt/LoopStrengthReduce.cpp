// ================================================================
// 循环强度削弱（Loop Strength Reduction）
// 借鉴 Cpl3 的 LoopStrengthReduce 设计
// 将循环内的 MUL 指令替换为累加（如 i*4 → addr+=4）
// 前提：需要 SCEV 分析确定归纳变量和步长
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// LSR 唯一命名计数器，避免多个循环的 LSR 指令同名（SSA 冲突）
static int lsrCounter = 0;

// ================================================================
// 判断一条指令是否是 MUL，且其中一个操作数是循环归纳变量
// 返回：(MUL指令, 归纳变量操作数, 常量操作数)
// ================================================================
struct MulInLoop {
    IR::Instruction* mulInst = nullptr;
    IR::Value* ivOperand = nullptr;     // 归纳变量操作数
    IR::ConstantInt* constOperand = nullptr;  // 常量操作数
};

MulInLoop findMulOfInductionVar(const NaturalLoop& loop, IR::Function* func) {
    MulInLoop result;
    auto info = analyzeLoopInduction(loop, func);
    if (info.tripCount < 0 || !info.var) return result;

    for (auto* bb : loop.body) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::MUL) continue;

            auto* op0 = inst->getOperand(0);
            auto* op1 = inst->getOperand(1);

            IR::Value* ivOp = nullptr;
            IR::ConstantInt* constOp = nullptr;

            // 检查 op0 是否是 LOAD 自归纳变量
            if (auto* op0Inst = dynamic_cast<IR::Instruction*>(op0)) {
                if (op0Inst->getOpcode() == IR::Instruction::Opcode::LOAD &&
                    op0Inst->getOperand(0) == info.var) {
                    ivOp = op0;
                    constOp = dynamic_cast<IR::ConstantInt*>(op1);
                }
            }
            // 检查 op1 是否是 LOAD 自归纳变量
            if (!ivOp && dynamic_cast<IR::Instruction*>(op1)) {
                auto* op1Inst = dynamic_cast<IR::Instruction*>(op1);
                if (op1Inst->getOpcode() == IR::Instruction::Opcode::LOAD &&
                    op1Inst->getOperand(0) == info.var) {
                    ivOp = op1;
                    constOp = dynamic_cast<IR::ConstantInt*>(op0);
                }
            }

            if (ivOp && constOp) {
                result.mulInst = inst.get();
                result.ivOperand = ivOp;
                result.constOperand = constOp;
                return result;
            }
        }
    }
    return result;
}

// ================================================================
// 强度削弱：将循环内的 MUL 替换为累加
// 对于 i * C 的 MUL，创建累加变量 accum，在循环外初始化为 0，
// 每次迭代 accum += C（而不是 i * C）
// ================================================================
bool strengthReduceLoop(const NaturalLoop& loop, IR::Function* func) {
    auto mul = findMulOfInductionVar(loop, func);
    if (!mul.mulInst) return false;

    auto info = analyzeLoopInduction(loop, func);
    if (info.tripCount < 0) return false;

    int64_t multiplier = mul.constOperand->getValue();
    int64_t stepVal = 0;
    if (auto* stepCI = dynamic_cast<IR::ConstantInt*>(info.step)) {
        stepVal = stepCI->getValue();
    } else {
        return false;
    }

    int64_t accumStep = multiplier * stepVal;
    auto* mulBB = mul.mulInst->getParent();

    // 1. 在 header 的 preheader 或 header 开头创建累加器的初始值
    // 初始值 = 0（如果起始值是 0）或 start * multiplier
    int64_t initialVal = 0;
    if (auto* startCI = dynamic_cast<IR::ConstantInt*>(info.start)) {
        initialVal = startCI->getValue() * multiplier;
    }

    auto* initConst = IR::ConstantInt::get(IR::IntegerType::get(32), initialVal);
    std::string suffix = std::to_string(lsrCounter++);
    auto* alloca = IR::Instruction::createAlloca(IR::IntegerType::get(32), "lsr.accum." + suffix);

    // 插入到 entry block 的 ALLOCA 之后
    auto* entry = func->getEntryBlock();
    auto entryIt = entry->begin();
    while (entryIt != entry->end() && (*entryIt)->getOpcode() == IR::Instruction::Opcode::ALLOCA) {
        ++entryIt;
    }
    entry->insert(entryIt, alloca);

    // 初始化累加器
    auto* initStore = IR::Instruction::createStore(initConst, alloca);
    entry->insert(entryIt, initStore);

    // 2. 在循环头中 LOAD 累加器
    auto* loadAccum = IR::Instruction::createLoad(IR::IntegerType::get(32), alloca, "lsr.load." + suffix);
    // 插入到 header 的第一个非 ALLOCA 指令之前
    auto headerIt = loop.header->begin();
    while (headerIt != loop.header->end() && (*headerIt)->getOpcode() == IR::Instruction::Opcode::ALLOCA) {
        ++headerIt;
    }
    loop.header->insert(headerIt, loadAccum);

    // 3. 替换 mulInst 的所有 uses 为 loadAccum
    mul.mulInst->replaceAllUsesWith(loadAccum);

    // 4. 在循环体末尾（STORE 归纳变量之后）累加 accumStep
    auto* stepConst = IR::ConstantInt::get(IR::IntegerType::get(32), accumStep);
    auto* addInst = IR::Instruction::createBinOp(
        IR::Instruction::Opcode::ADD, IR::IntegerType::get(32), "lsr.add." + suffix,
        loadAccum, stepConst);

    // 找到 STORE 到归纳变量的位置，在其后插入
    bool inserted = false;
    for (auto* bb : loop.body) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if ((*it)->getOpcode() == IR::Instruction::Opcode::STORE) {
                auto* storePtr = (*it)->getOperand(1);
                if (storePtr == info.var) {
                    ++it;
                    bb->insert(it, addInst);
                    // 插入 STORE 累加器
                    auto* storeAccum = IR::Instruction::createStore(addInst, alloca);
                    bb->insert(it, storeAccum);
                    inserted = true;
                    break;
                }
            }
        }
        if (inserted) break;
    }

    return true;
}

} // namespace

bool loopStrengthReduce(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        auto loops = findNaturalLoops(func.get());
        for (auto& loop : loops) {
            if (strengthReduceLoop(loop, func.get())) changed = true;
        }
    }
    return changed;
}

} // namespace Opt