// ================================================================
// 纯函数识别（PureFuncDect，借鉴 Cpl5）
//
// 纯函数定义（本项目口径）：
//   1. 不 STORE 到非局部内存（全局变量、参数指针、不可追踪目标）
//      —— 允许 STORE 到本函数局部 ALLOCA（含 GEP(ALLOCA)）
//   2. 不调用非纯函数（含外部函数：sylib IO 等有副作用）
//   3. 允许 LOAD 全局变量（读内存不产生副作用）
//
// 算法：乐观初始化所有函数为纯，迭代标记非纯直至不动点。
//   自递归/相互递归天然支持：递归边在乐观初值下不污染纯度，
//   只有真实副作用指令才会传播非纯标记。
//
// 用途：
//   - LICM：纯 CALL 不阻塞 LOAD 提升；纯 CALL 本身在参数全为
//     循环不变量时可外提（结果只依赖参数，循环各迭代相同）
//   - LoadElimination：跨纯 CALL 保留 LOAD 缓存（纯函数不写内存）
// ================================================================

#include "opt/Optimizer.h"

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// ---- 追踪 STORE 目标指针的最终基址 ----
// 穿过 GEP 链，返回底层 Value（ALLOCA / GlobalVariable / Argument / 其他）
IR::Value* traceToBase(IR::Value* ptr) {
    while (auto* inst = dynamic_cast<IR::Instruction*>(ptr)) {
        if (inst->getOpcode() == Opc::GETELEMENTPTR) {
            ptr = inst->getOperand(0);
            continue;
        }
        return inst;  // ALLOCA / CALL / LOAD / PHI 等
    }
    return ptr;  // GlobalVariable / Argument / Constant
}

// ---- 判断单个函数体内是否含"直接非纯行为"（不含 CALL 传播）----
// directImpure = true 表示函数本身有无副作用指令（STORE 到非局部）
bool hasDirectSideEffect(IR::Function* func) {
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();
            if (op == Opc::STORE) {
                // STORE 的第 2 操作数是目标地址
                auto* base = traceToBase(inst->getOperand(1));
                if (auto* baseInst = dynamic_cast<IR::Instruction*>(base)) {
                    if (baseInst->getOpcode() == Opc::ALLOCA) {
                        continue;  // 写局部栈帧，无副作用
                    }
                    // CALL/LOAD/PHI 结果作为地址：可能指向全局或参数内存
                    return true;
                }
                // GlobalVariable / Argument 作为地址：写调用者可见内存
                return true;
            }
        }
    }
    return false;
}

// ---- 判断 CALL 的 callee（Function* 或 nullptr 表示不可解析）----
IR::Function* getCallee(IR::Instruction* inst) {
    if (inst->getOpcode() != Opc::CALL) return nullptr;
    return dynamic_cast<IR::Function*>(inst->getOperand(0));
}

} // namespace

bool isPureFunction(IR::Function* func,
    const std::unordered_set<IR::Function*>& pureSet) {
    return func && pureSet.count(func) > 0;
}

std::unordered_set<IR::Function*> detectPureFunctions(IR::Module* mod) {
    std::unordered_set<IR::Function*> pure;

    // 1. 乐观初始化：所有非外部函数为纯，且记录有直接副作用的函数
    std::unordered_set<IR::Function*> impure;
    for (auto& func : mod->getFunctions()) {
        auto* f = func.get();
        if (f->isExternal()) {
            impure.insert(f);  // 外部函数（sylib IO 等）一律非纯
            continue;
        }
        if (hasDirectSideEffect(f)) {
            impure.insert(f);
        } else {
            pure.insert(f);
        }
    }

    // 2. 迭代传播：CALL 到非纯/不可解析 callee → caller 非纯
    bool changed = true;
    while (changed) {
        changed = false;
        // 收集本轮要降级为非纯的函数（避免遍历时改容器）
        std::vector<IR::Function*> toDemote;
        for (auto* f : pure) {
            for (auto& bb : f->getBlocks()) {
                for (auto& inst : bb->getInstructions()) {
                    if (inst->getOpcode() != Opc::CALL) continue;
                    auto* callee = getCallee(inst.get());
                    if (!callee || impure.count(callee)) {
                        toDemote.push_back(f);
                        goto nextFunc;  // 该函数已确定非纯，跳到下一个
                    }
                }
            }
            nextFunc:;
        }
        for (auto* f : toDemote) {
            pure.erase(f);
            impure.insert(f);
            changed = true;
        }
    }
    return pure;
}

} // namespace Opt
