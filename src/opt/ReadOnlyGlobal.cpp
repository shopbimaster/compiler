// ================================================================
// 只读全局变量分析
// 扫描所有函数中所有指令，判定哪些全局变量事实上从未被写入
// 结果可被 LICM 和 DSE 使用，提升优化精度
// 借鉴 Cpl3 的 ReadOnlyGlobal 分析
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_set>

namespace Opt {
namespace {

// 收集整个模块中所有被 STORE 的全局变量
// 同时检查 CALL 指令：如果函数非纯函数，保守标记所有全局变量为"可能被写"
std::unordered_set<IR::GlobalVariable*> collectModuleStoredGlobals(IR::Module* mod) {
    std::unordered_set<IR::GlobalVariable*> storedGlobals;

    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) {
            // 外部函数可能写入任意全局变量 — 保守标记所有全局变量
            for (auto& gv : mod->getGlobals()) {
                storedGlobals.insert(gv.get());
            }
            return storedGlobals;
        }
        for (auto& bb : func->getBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                    auto* ptr = inst->getOperand(1);
                    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(ptr)) {
                        storedGlobals.insert(gv);
                    }
                }
                // CALL 指令：保守假设被调用函数可能修改任意全局变量
                // 除非我们能证明被调用函数是纯函数
                if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                    auto* callee = inst->getOperand(0);
                    // 如果是外部函数（如 sysy 库函数），保守处理
                    if (auto* calledFunc = dynamic_cast<IR::Function*>(callee)) {
                        if (calledFunc->isExternal()) {
                            for (auto& gv : mod->getGlobals()) {
                                storedGlobals.insert(gv.get());
                            }
                            return storedGlobals;
                        }
                    }
                }
            }
        }
    }
    return storedGlobals;
}

} // namespace

// 返回模块中真正只读的全局变量集合
// 只读 = 在任何函数中都没有 STORE 到该全局变量
// 注意：const 全局变量天然是只读的（.rodata 段），也应包含在内
std::unordered_set<IR::GlobalVariable*> readOnlyGlobalAnalysis(IR::Module* mod) {
    auto storedGlobals = collectModuleStoredGlobals(mod);
    std::unordered_set<IR::GlobalVariable*> readOnly;

    for (auto& gv : mod->getGlobals()) {
        if (!storedGlobals.count(gv.get())) {
            readOnly.insert(gv.get());
        }
    }
    return readOnly;
}

} // namespace Opt