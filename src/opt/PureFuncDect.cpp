// ================================================================
// PureFuncDect — 纯函数分析
// 借鉴 Cpl5：标记纯函数，让 LICM 提升 CALL，是 GVN 跨 CALL 消除的前置
//
// 纯函数定义（结果仅依赖于标量参数，无外部可见副作用）：
//   1. 不 STORE 到外部可见内存（全局变量或函数参数指针）
//   2. 不 LOAD 从外部可变内存（全局变量或函数参数指针）
//      LOAD 从局部 ALLOCA 不影响纯性
//   3. 不 CALL 非纯函数（包括外部函数如 getint/putint）
//   4. 不直接递归（简化分析）
//
// ★ 关键：LOAD 从指针参数的函数不算纯
//   例如 model(int a[][5]) 读取 a 的内容，a 指向的内存可能在循环中改变
//   即使指针参数本身不变，指向的内容变了 → 结果变化 → 不可外提
//
// 用途：
//   - LICM：纯函数调用在标量参数不变时可外提到循环外
//   - LICM：循环内仅调用纯函数时，LOAD 全局变量也可外提
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {

// 追踪指针的基地址：透过 GEP 找到根指针
// 用于判断 LOAD/STORE 的目标是否为外部可见内存
static IR::Value* traceBasePointer(IR::Value* ptr) {
    while (auto* inst = dynamic_cast<IR::Instruction*>(ptr)) {
        if (inst->getOpcode() == IR::Instruction::Opcode::GETELEMENTPTR) {
            ptr = inst->getOperand(0);
        } else {
            break;  // 非 GEP 指令，停止追踪
        }
    }
    return ptr;
}

// 检查指针是否指向外部可见内存
// 外部可见：全局变量或函数参数（调用者的内存）
// 局部 ALLOCA 不算外部可见
static bool pointsToExternalMemory(IR::Value* ptr, IR::Function* func) {
    auto* base = traceBasePointer(ptr);

    // 基地址是全局变量 → 外部可见
    if (dynamic_cast<IR::GlobalVariable*>(base)) return true;

    // 基地址是函数参数 → 可能指向调用者的内存
    for (unsigned i = 0; i < func->getNumArgs(); ++i) {
        if (base == func->getArg(i)) return true;
    }

    // 基地址是局部 ALLOCA → 内部内存
    if (auto* inst = dynamic_cast<IR::Instruction*>(base)) {
        if (inst->getOpcode() == IR::Instruction::Opcode::ALLOCA) return false;
    }

    // 其他情况（如 LOAD 出来的指针）→ 保守视为外部可见
    return true;
}

// 分析模块中的纯函数，返回纯函数集合
// 使用不动点迭代：函数的纯性依赖于其调用的函数的纯性
std::unordered_set<IR::Function*> computePureFunctions(IR::Module* mod) {
    std::unordered_set<IR::Function*> pure;

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;       // 外部函数默认非纯
            if (pure.count(func.get())) continue;   // 已标记为纯

            bool isPure = true;
            for (auto& bb : func->getBlocks()) {
                for (auto& inst : bb->getInstructions()) {
                    auto op = inst->getOpcode();

                    // STORE 到外部内存 → 非纯（有副作用）
                    if (op == IR::Instruction::Opcode::STORE) {
                        if (pointsToExternalMemory(inst->getOperand(1), func.get())) {
                            isPure = false;
                            break;
                        }
                    }

                    // LOAD 从外部内存 → 非纯（结果依赖可变外部状态）
                    // ★ 这是 71_full_conn 修复的关键：model(a) 读取指针参数 a
                    //   的内容，a 指向的内存可能在循环中改变，不可外提
                    if (op == IR::Instruction::Opcode::LOAD) {
                        if (pointsToExternalMemory(inst->getOperand(0), func.get())) {
                            isPure = false;
                            break;
                        }
                    }

                    // CALL 检查
                    if (op == IR::Instruction::Opcode::CALL) {
                        auto* callee = dynamic_cast<IR::Function*>(inst->getOperand(0));
                        if (!callee) { isPure = false; break; }
                        // 直接递归 → 视为非纯（简化分析）
                        if (callee == func.get()) { isPure = false; break; }
                        // 外部函数 → 非纯
                        if (callee->isExternal()) { isPure = false; break; }
                        // 调用未标记为纯的函数 → 暂时非纯
                        if (!pure.count(callee)) { isPure = false; break; }
                    }
                }
                if (!isPure) break;
            }

            if (isPure) {
                pure.insert(func.get());
                changed = true;
            }
        }
    }
    return pure;
}

} // namespace Opt
