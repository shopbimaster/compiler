// ================================================================
// 全局常量传播 + 跨函数参数常量传播（简易 IPSCCP）
//
// 策略：
//   Step 1: 模块级可写全局变量分析
//   Step 2: 将未被 STORE 且有 ConstantInt 初始值的全局变量 LOAD 替换为常量
//   Step 3: 收集每个函数所有 call site 的实参
//   Step 4: 对于非 external 函数，如果某个参数在所有 call site 中都是
//           相同的 ConstantInt，将函数内对该参数的使用替换为常量
//
// 这使得后续 SCCP 能在函数内传播常量到循环边界，
// 触发循环完全展开等优化（如 h-5 的 n=20 传播到 kernel_ludcmp）
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace Opt {

bool globalConstantPropagation(IR::Module* mod) {
    bool anyChanged = false;

    // ================================================================
    // Step 1: 收集被 STORE 的全局变量（模块级别可写集合）
    // ================================================================
    std::unordered_set<IR::GlobalVariable*> writtenGlobals;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        for (auto& bb : func->getBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                    if (inst->getNumOperands() >= 2) {
                        if (auto* gv = dynamic_cast<IR::GlobalVariable*>(inst->getOperand(1))) {
                            writtenGlobals.insert(gv);
                        }
                    }
                }
            }
        }
    }

    // ================================================================
    // Step 2: 收集可传播的全局变量（有 ConstantInt 初始值且未被 STORE）
    // ================================================================
    std::unordered_map<IR::GlobalVariable*, IR::ConstantInt*> constGlobals;
    for (auto& gvUp : mod->getGlobals()) {
        auto* gv = gvUp.get();
        if (!gv) continue;
        if (writtenGlobals.count(gv)) continue;
        auto* ci = dynamic_cast<IR::ConstantInt*>(gv->getInitializer());
        if (ci) {
            constGlobals[gv] = ci;
        }
    }

    // Step 2b: 将所有 LOAD 可传播全局变量替换为 ConstantInt
    if (!constGlobals.empty()) {
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                for (auto it = bb->begin(); it != bb->end(); ) {
                    auto* inst = it->get();
                    if (inst->getOpcode() != IR::Instruction::Opcode::LOAD) {
                        ++it;
                        continue;
                    }
                    if (inst->getNumOperands() < 1) { ++it; continue; }
                    auto* gv = dynamic_cast<IR::GlobalVariable*>(inst->getOperand(0));
                    if (!gv) { ++it; continue; }
                    auto cgIt = constGlobals.find(gv);
                    if (cgIt == constGlobals.end()) { ++it; continue; }
                    inst->replaceAllUsesWith(cgIt->second);
                    inst->dropAllUses();
                    it = bb->erase(it);
                    anyChanged = true;
                }
            }
        }
    }

    // ================================================================
    // Step 3: 跨函数参数常量传播（简易 IPSCCP）
    //   对于每个非 external 函数，收集所有 call site 的实参。
    //   如果某个参数在所有 call site 中都是相同的 ConstantInt，
    //   将函数内对该参数的使用替换为常量。
    // ================================================================

    // 收集每个函数的所有 call site 实参
    // callArgs[func] = vector of vector<ConstantInt*> (每个 call site 的实参)
    std::unordered_map<IR::Function*, std::vector<std::vector<IR::Value*>>> callSites;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        for (auto& bb : func->getBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() != IR::Instruction::Opcode::CALL) continue;
                if (inst->getNumOperands() < 1) continue;
                auto* callee = dynamic_cast<IR::Function*>(inst->getOperand(0));
                if (!callee || callee->isExternal()) continue;
                std::vector<IR::Value*> args;
                for (unsigned i = 1; i < inst->getNumOperands(); ++i) {
                    args.push_back(inst->getOperand(i));
                }
                callSites[callee].push_back(std::move(args));
            }
        }
    }

    // 对每个函数，检查哪些参数可以传播常量
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        auto it = callSites.find(func.get());
        if (it == callSites.end() || it->second.empty()) continue;

        const auto& sites = it->second;
        unsigned numArgs = func->getNumArgs();
        for (unsigned argIdx = 0; argIdx < numArgs; ++argIdx) {
            // 检查所有 call site 的该参数是否都是相同的 ConstantInt
            IR::ConstantInt* commonConst = nullptr;
            bool allSame = true;
            for (const auto& site : sites) {
                if (argIdx >= site.size()) { allSame = false; break; }
                auto* ci = dynamic_cast<IR::ConstantInt*>(site[argIdx]);
                if (!ci) { allSame = false; break; }
                if (!commonConst) {
                    commonConst = ci;
                } else if (ci->getValue() != commonConst->getValue()) {
                    allSame = false;
                    break;
                }
            }
            if (!allSame || !commonConst) continue;

            // 将函数内对该参数的所有使用替换为常量
            IR::Value* arg = func->getArg(argIdx);
            arg->replaceAllUsesWith(commonConst);
            anyChanged = true;
        }
    }

    return anyChanged;
}

} // namespace Opt
