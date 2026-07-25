// ================================================================
// GEPFolding — 嵌套 GEP 合并
//
// 合并 GEP(GEP(ptr, i, ...), 0, j, ...) 为 GEP(ptr, i, ..., j, ...)
// 当内层 GEP 单使用且外层 GEP 首索引为 0 时安全合并。
//
// 安全性：
//   - 内层 GEP 必须只有一个使用者（外层 GEP），否则会重复计算
//   - 外层 GEP 首索引必须为常量 0（避免索引类型不匹配）
//   - 合并后，codegen 的 collectFoldedGeps 可以将结果 GEP 融合到
//     LOAD/STORE，消除独立地址计算指令
//
// 借鉴 Cpl7 GEPFolding。
// ================================================================

#include "opt/Optimizer.h"
#include <cstdio>
#include <unordered_set>
#include <vector>

namespace Opt {

bool gepFolding(IR::Module* mod) {
    bool changed = false;

    for (auto& func : mod->getFunctions()) {
        if (func->isExternal() || func->getBlocks().empty())
            continue;

        // 迭代直到收敛：每轮处理一个 fold，然后重新扫描
        // 避免 multi-level GEP 链（A→B→C）中删除 B 后 C 的 inner 指针悬空
        bool funcChanged = true;
        while (funcChanged) {
            funcChanged = false;

            for (auto& bb : func->getBlocks()) {
                bool bbChanged = true;
                while (bbChanged) {
                    bbChanged = false;
                    for (auto it = bb->begin(); it != bb->end(); ++it) {
                        auto* inst = it->get();
                        if (inst->getOpcode() != IR::Instruction::Opcode::GETELEMENTPTR)
                            continue;

                        // 检查 base pointer (operand 0) 是否是另一个 GEP
                        auto* baseInst = dynamic_cast<IR::Instruction*>(inst->getOperand(0));
                        if (!baseInst ||
                            baseInst->getOpcode() != IR::Instruction::Opcode::GETELEMENTPTR)
                            continue;

                        // 内层 GEP 必须只有一个使用者（外层 GEP）
                        if (!baseInst->hasOneUse())
                            continue;

                        // 外层 GEP 首索引必须为常量 0
                        auto* outerFirstIdx =
                            dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
                        if (!outerFirstIdx || outerFirstIdx->getValue() != 0)
                            continue;

                        // 合并：new GEP = GEP(inner.base, inner.indices..., outer.indices[1:])
                        std::vector<IR::Value*> newOperands;
                        newOperands.push_back(baseInst->getOperand(0));
                        for (unsigned i = 1; i < baseInst->getNumOperands(); ++i) {
                            newOperands.push_back(baseInst->getOperand(i));
                        }
                        for (unsigned i = 2; i < inst->getNumOperands(); ++i) {
                            newOperands.push_back(inst->getOperand(i));
                        }

                        auto* innerBasePtrTy =
                            dynamic_cast<IR::PointerType*>(baseInst->getOperand(0)->getType());
                        IR::Type* pointee =
                            innerBasePtrTy ? innerBasePtrTy->getPointeeType() : IR::IntegerType::I32;

                        std::vector<IR::Value*> indices;
                        for (size_t i = 1; i < newOperands.size(); ++i) {
                            indices.push_back(newOperands[i]);
                        }

                        auto* newGEP = IR::Instruction::createGetElementPtr(
                        pointee, newOperands[0], indices, inst->getName());

                    if (std::getenv("DEBUG_GEP_FOLD")) {
                        std::fprintf(stderr, "GEP FOLD: %s (base=%s) + %s → %s\n",
                                     baseInst->getName().c_str(),
                                     baseInst->getOperand(0)->getName().c_str(),
                                     inst->getName().c_str(),
                                     newGEP->getName().c_str());
                    }

                        // ★ 插入新 GEP（insert 返回新元素的迭代器，原 it 失效）
                        auto newIt = bb->insert(it, newGEP);
                        // newIt 指向 newGEP，newIt+1 指向外层 GEP (inst)

                        // 替换外层 GEP 的所有使用
                        inst->replaceAllUsesWith(newGEP);
                        inst->dropAllUses();

                        // 删除外层 GEP（在 newIt+1 位置）
                        auto outerIt = newIt + 1;
                        bb->erase(outerIt);

                        // 删除内层 GEP（可能在不同 BB）
                        baseInst->dropAllUses();
                        auto* innerBB = baseInst->getParent();
                        for (auto it2 = innerBB->begin(); it2 != innerBB->end(); ++it2) {
                            if (it2->get() == baseInst) {
                                innerBB->erase(it2);
                                break;
                            }
                        }

                        bbChanged = true;
                        funcChanged = true;
                        changed = true;
                        break;  // 重新扫描，避免迭代器失效
                    }
                }
            }
        }
    }

    return changed;
}

} // namespace Opt
