// ================================================================
// O2: 树摇（TreeShaking）— 移除未使用的函数和全局变量
// 借鉴 Cpl1 的 TreeShaking 设计
// 策略：
//   1. 移除 useCount 为 0 的用户定义函数（保留 @main 和外部声明）
//   2. 移除 useCount 为 0 的全局变量
// ================================================================

#include "opt/Optimizer.h"
#include <iostream>

namespace Opt {

bool treeShaking(IR::Module* mod) {
    bool changed = false;

    // 第一遍：移除未使用的函数
    // 使用手动循环而非 removeFunctionsIf，避免 std::remove_if 的 unique_ptr 移动语义问题
    {
        std::vector<IR::Function*> toRemove;
        for (auto& fn : mod->getFunctions()) {
            if (fn->getName() == "main") continue;  // 保留入口
            if (fn->isExternal()) continue;            // 保留外部声明
            if (fn->getNumUses() == 0) {
                toRemove.push_back(fn.get());
            }
        }
        if (!toRemove.empty()) {
            changed = true;
            mod->removeFunctionsIf([&](IR::Function* fn) {
                for (auto* r : toRemove) {
                    if (r == fn) return true;
                }
                return false;
            });
        }
    }

    // 第二遍：移除未使用的全局变量
    {
        std::vector<IR::GlobalVariable*> toRemove;
        for (auto& gv : mod->getGlobals()) {
            if (gv->getNumUses() == 0) {
                toRemove.push_back(gv.get());
            }
        }
        if (!toRemove.empty()) {
            changed = true;
            mod->removeGlobalsIf([&](IR::GlobalVariable* gv) {
                for (auto* r : toRemove) {
                    if (r == gv) return true;
                }
                return false;
            });
        }
    }

    return changed;
}

} // namespace Opt