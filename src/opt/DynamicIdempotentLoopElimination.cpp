// ================================================================
// src/opt/DynamicIdempotentLoopElimination.cpp — 动态幂等循环迭代消除
// ----------------------------------------------------------------
// 所属模块：opt（O2 结构化变换）
// 关键依赖：opt/LoopAnalysis.h（循环模式分析）、opt/Optimizer.h
// ================================================================

#include "opt/LoopAnalysis.h"
#include "opt/Optimizer.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

struct GlobalEffects {
    unsigned loads = 0;
    unsigned stores = 0;
};

struct FixedPointLoopPlan {
    IR::BasicBlock* body = nullptr;
    IR::Instruction* countLoad = nullptr;
    IR::Instruction* decrement = nullptr;
    IR::GlobalVariable* carry = nullptr;
    int64_t initialCount = 0;
};

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool isLoadFrom(IR::Value* value, IR::Value* pointer) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    return load && load->getOpcode() == Opc::LOAD &&
           load->getNumOperands() == 1 && load->getOperand(0) == pointer;
}

bool isDirectAlloca(IR::Value* value) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    return instruction && instruction->getOpcode() == Opc::ALLOCA;
}

bool findUniquePreheader(
    IR::Function* function,
    const std::unordered_set<IR::BasicBlock*>& body,
    IR::BasicBlock* header, IR::BasicBlock*& preheader) {
    auto predecessors = buildPredecessors(function);
    preheader = nullptr;
    for (auto* predecessor : predecessors[header]) {
        if (body.count(predecessor)) continue;
        if (preheader) return false;
        preheader = predecessor;
    }
    auto* terminator = preheader ? preheader->getTerminator() : nullptr;
    return terminator && terminator->getOpcode() == Opc::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == header;
}

IR::Function* calledFunction(IR::Instruction* instruction) {
    if (!instruction || instruction->getOpcode() != Opc::CALL ||
        instruction->getNumOperands() == 0) {
        return nullptr;
    }
    return dynamic_cast<IR::Function*>(instruction->getOperand(0));
}

bool collectCalleeClosureImpl(
    IR::Function* function,
    std::unordered_set<IR::Function*>& visiting,
    std::unordered_set<IR::Function*>& closure) {
    if (!function || function->isExternal() ||
        !visiting.insert(function).second) {
        return false;
    }
    for (unsigned index = 0; index < function->getNumArgs(); ++index) {
        if (function->getArg(index)->getType()->isPointer()) return false;
    }
    for (const auto& block : function->getBlocks()) {
        for (const auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() == Opc::CALL) {
                auto* callee = calledFunction(instruction);
                if (!callee || callee->isExternal()) return false;
                if (!closure.count(callee) &&
                    !collectCalleeClosureImpl(callee, visiting, closure)) {
                    return false;
                }
                continue;
            }
            if (instruction->getOpcode() != Opc::LOAD &&
                instruction->getOpcode() != Opc::STORE) {
                continue;
            }
            const unsigned pointerOperand =
                instruction->getOpcode() == Opc::LOAD ? 0 : 1;
            auto* pointer = instruction->getOperand(pointerOperand);
            if (!isDirectAlloca(pointer) && !rootGlobal(pointer)) return false;
        }
    }
    visiting.erase(function);
    closure.insert(function);
    return true;
}

bool collectCalleeClosure(
    const std::vector<IR::Function*>& roots,
    std::unordered_set<IR::Function*>& closure) {
    std::unordered_set<IR::Function*> visiting;
    for (auto* root : roots) {
        if (!closure.count(root) &&
            !collectCalleeClosureImpl(root, visiting, closure)) {
            return false;
        }
    }
    return true;
}

void addGlobalEffects(
    IR::Function* function,
    std::unordered_map<IR::GlobalVariable*, GlobalEffects>& effects) {
    for (const auto& block : function->getBlocks()) {
        for (const auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() != Opc::LOAD &&
                instruction->getOpcode() != Opc::STORE) {
                continue;
            }
            const unsigned pointerOperand =
                instruction->getOpcode() == Opc::LOAD ? 0 : 1;
            auto* global = rootGlobal(instruction->getOperand(pointerOperand));
            if (!global) continue;
            auto& effect = effects[global];
            if (instruction->getOpcode() == Opc::LOAD) ++effect.loads;
            if (instruction->getOpcode() == Opc::STORE) ++effect.stores;
        }
    }
}

void addGlobalEffects(
    IR::BasicBlock* block,
    std::unordered_map<IR::GlobalVariable*, GlobalEffects>& effects) {
    for (const auto& owned : block->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction->getOpcode() != Opc::LOAD &&
            instruction->getOpcode() != Opc::STORE) {
            continue;
        }
        const unsigned pointerOperand =
            instruction->getOpcode() == Opc::LOAD ? 0 : 1;
        auto* global = rootGlobal(instruction->getOperand(pointerOperand));
        if (!global) continue;
        auto& effect = effects[global];
        if (instruction->getOpcode() == Opc::LOAD) ++effect.loads;
        if (instruction->getOpcode() == Opc::STORE) ++effect.stores;
    }
}

// A per-iteration reset is a semantic fact only for a directly addressed
// scalar.  Resetting one element through a GEP does not reset the rest of an
// aggregate global, so aggregate accesses must remain part of the carried
// state considered below.
bool hasDirectScalarResetBeforeCalls(
    IR::BasicBlock* body, IR::GlobalVariable* global) {
    if (!global) return false;
    auto* pointerType = dynamic_cast<IR::PointerType*>(global->getType());
    if (!pointerType ||
        pointerType->getPointeeType() != IR::IntegerType::I32) {
        return false;
    }

    bool reset = false;
    for (const auto& owned : body->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction->getOpcode() == Opc::CALL) return reset;
        if (instruction->getOpcode() == Opc::LOAD &&
            instruction->getOperand(0) == global) {
            return false;
        }
        if (instruction->getOpcode() == Opc::STORE &&
            instruction->getOperand(1) == global) {
            if (reset || !isConstant(instruction->getOperand(0), 0)) {
                return false;
            }
            reset = true;
        }
    }
    return false;
}

bool isZeroInitializedScalar(IR::GlobalVariable* global) {
    if (!global || !global->getInitData().empty()) return false;
    auto* pointer = dynamic_cast<IR::PointerType*>(global->getType());
    if (!pointer || pointer->getPointeeType() != IR::IntegerType::I32) {
        return false;
    }
    auto* initializer = global->getInitializer();
    return !initializer || isConstant(initializer, 0);
}

bool carryHasNoEscapes(IR::GlobalVariable* carry) {
    for (const auto& use : carry->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction ||
            (instruction->getOpcode() != Opc::LOAD &&
             instruction->getOpcode() != Opc::STORE)) {
            return false;
        }
        const unsigned pointerOperand =
            instruction->getOpcode() == Opc::LOAD ? 0 : 1;
        if (instruction->getNumOperands() <= pointerOperand ||
            instruction->getOperand(pointerOperand) != carry) {
            return false;
        }
    }
    return true;
}

bool closureHasNoOutsideCallsOrCarryStores(
    IR::Module* module, IR::Function* owner,
    const std::unordered_set<IR::BasicBlock*>& loopBody,
    const std::unordered_set<IR::Function*>& closure,
    IR::GlobalVariable* carry) {
    for (const auto& function : module->getFunctions()) {
        for (const auto& block : function->getBlocks()) {
            for (const auto& owned : block->getInstructions()) {
                auto* instruction = owned.get();
                if (instruction->getOpcode() == Opc::CALL) {
                    auto* callee = calledFunction(instruction);
                    if (callee && closure.count(callee) &&
                        !closure.count(function.get()) &&
                        !(function.get() == owner &&
                          loopBody.count(block.get()))) {
                        return false;
                    }
                }
                if (instruction->getOpcode() == Opc::STORE &&
                    rootGlobal(instruction->getOperand(1)) == carry &&
                    !closure.count(function.get())) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool localLoopStateIsOverwritten(
    IR::BasicBlock* body, IR::Value* countPointer) {
    std::unordered_map<IR::Value*, GlobalEffects> effects;
    for (const auto& owned : body->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction->getOpcode() != Opc::LOAD &&
            instruction->getOpcode() != Opc::STORE) {
            continue;
        }
        const unsigned pointerOperand =
            instruction->getOpcode() == Opc::LOAD ? 0 : 1;
        auto* pointer = instruction->getOperand(pointerOperand);
        if (!isDirectAlloca(pointer) || pointer == countPointer) continue;
        auto& effect = effects[pointer];
        if (instruction->getOpcode() == Opc::LOAD) ++effect.loads;
        if (instruction->getOpcode() == Opc::STORE) ++effect.stores;
    }
    return std::all_of(effects.begin(), effects.end(), [](const auto& item) {
        return item.second.stores > 0 && item.second.loads == 0;
    });
}

bool matchCountDownLoop(
    IR::Function* function, const NaturalLoop& loop,
    IR::BasicBlock*& body, IR::Value*& countPointer,
    IR::Instruction*& countLoad, IR::Instruction*& decrement,
    int64_t& initialCount) {
    if (loop.body.size() != 2) return false;
    auto* branch = loop.header ? loop.header->getTerminator() : nullptr;
    auto* compare = branch && branch->getOpcode() == Opc::COND_BR
        ? dynamic_cast<IR::Instruction*>(branch->getOperand(0))
        : nullptr;
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        compare->getNumOperands() != 2) {
        return false;
    }
    if (compare->getName() == "sgt" &&
        isConstant(compare->getOperand(1), 0)) {
        auto* load = dynamic_cast<IR::Instruction*>(compare->getOperand(0));
        countPointer = load && load->getOpcode() == Opc::LOAD &&
                load->getNumOperands() == 1
            ? load->getOperand(0)
            : nullptr;
    } else if (compare->getName() == "slt" &&
               isConstant(compare->getOperand(0), 0)) {
        auto* load = dynamic_cast<IR::Instruction*>(compare->getOperand(1));
        countPointer = load && load->getOpcode() == Opc::LOAD &&
                load->getNumOperands() == 1
            ? load->getOperand(0)
            : nullptr;
    }
    if (!countPointer ||
        (!isLoadFrom(compare->getOperand(0), countPointer) &&
         !isLoadFrom(compare->getOperand(1), countPointer)) ||
        !isDirectAlloca(countPointer)) {
        return false;
    }

    body = dynamic_cast<IR::BasicBlock*>(branch->getOperand(1));
    auto* exit = dynamic_cast<IR::BasicBlock*>(branch->getOperand(2));
    if (!body || !exit || !loop.body.count(body) || loop.body.count(exit) ||
        body == loop.header) {
        return false;
    }
    auto* bodyTerminator = body->getTerminator();
    if (!bodyTerminator || bodyTerminator->getOpcode() != Opc::BR ||
        bodyTerminator->getNumOperands() != 1 ||
        bodyTerminator->getOperand(0) != loop.header) {
        return false;
    }

    IR::BasicBlock* preheader = nullptr;
    if (!findUniquePreheader(function, loop.body, loop.header, preheader)) {
        return false;
    }
    IR::Instruction* initStore = nullptr;
    for (const auto& owned : preheader->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction->getOpcode() != Opc::STORE ||
            instruction->getNumOperands() != 2 ||
            instruction->getOperand(1) != countPointer) {
            continue;
        }
        auto* constant = dynamic_cast<IR::ConstantInt*>(instruction->getOperand(0));
        if (initStore || !constant || constant->getValue() < 2 ||
            constant->getValue() > 1000000) {
            return false;
        }
        initStore = instruction;
        initialCount = constant->getValue();
    }
    if (!initStore) return false;

    countLoad = nullptr;
    decrement = nullptr;
    IR::Instruction* decrementStore = nullptr;
    for (const auto& owned : body->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction->getOpcode() != Opc::STORE ||
            instruction->getNumOperands() != 2 ||
            instruction->getOperand(1) != countPointer) {
            continue;
        }
        auto* candidate = dynamic_cast<IR::Instruction*>(instruction->getOperand(0));
        if (decrementStore || !candidate ||
            candidate->getOpcode() != Opc::SUB ||
            candidate->getNumOperands() != 2 ||
            !isConstant(candidate->getOperand(1), 1) ||
            !isLoadFrom(candidate->getOperand(0), countPointer)) {
            return false;
        }
        decrementStore = instruction;
        decrement = candidate;
        countLoad = dynamic_cast<IR::Instruction*>(candidate->getOperand(0));
    }
    if (!decrementStore || !decrement || !countLoad ||
        body->getInstructions().size() < 4) {
        return false;
    }
    const auto count = body->getInstructions().size();
    return body->getInstructions()[count - 4].get() == countLoad &&
           body->getInstructions()[count - 3].get() == decrement &&
           body->getInstructions()[count - 2].get() == decrementStore &&
           body->getInstructions()[count - 1]->getOpcode() == Opc::BR;
}

bool matchFixedPointLoop(
    IR::Module* module, IR::Function* function,
    const NaturalLoop& loop, FixedPointLoopPlan& plan) {
    IR::BasicBlock* body = nullptr;
    IR::Value* countPointer = nullptr;
    IR::Instruction* countLoad = nullptr;
    IR::Instruction* decrement = nullptr;
    int64_t initialCount = 0;
    if (!matchCountDownLoop(
            function, loop, body, countPointer,
            countLoad, decrement, initialCount) ||
        !localLoopStateIsOverwritten(body, countPointer)) {
        return false;
    }

    std::vector<IR::Function*> roots;
    for (const auto& owned : body->getInstructions()) {
        if (owned->getOpcode() != Opc::CALL) continue;
        auto* callee = calledFunction(owned.get());
        if (!callee || callee->isExternal()) return false;
        roots.push_back(callee);
    }
    if (roots.empty()) return false;
    std::unordered_set<IR::Function*> closure;
    if (!collectCalleeClosure(roots, closure)) return false;

    std::unordered_map<IR::GlobalVariable*, GlobalEffects> effects;
    addGlobalEffects(body, effects);
    for (auto* callee : closure) addGlobalEffects(callee, effects);

    std::vector<IR::GlobalVariable*> carries;
    for (const auto& [global, effect] : effects) {
        if (effect.loads == 0 || effect.stores == 0) continue;
        if (hasDirectScalarResetBeforeCalls(body, global)) continue;
        carries.push_back(global);
    }
    if (carries.size() != 1 ||
        !isZeroInitializedScalar(carries.front()) ||
        !carryHasNoEscapes(carries.front()) ||
        !closureHasNoOutsideCallsOrCarryStores(
            module, function, loop.body, closure, carries.front())) {
        return false;
    }

    plan.body = body;
    plan.countLoad = countLoad;
    plan.decrement = decrement;
    plan.carry = carries.front();
    plan.initialCount = initialCount;
    return true;
}

void applyFixedPointGuard(const FixedPointLoopPlan& plan) {
    auto position = std::find_if(
        plan.body->begin(), plan.body->end(), [&](const auto& instruction) {
            return instruction.get() == plan.decrement;
        });
    if (position == plan.body->end()) return;
    auto* i32 = IR::IntegerType::I32;
    auto* carryLoad = IR::Instruction::createLoad(
        i32, plan.carry, "fixedpoint.carry");
    auto* carryReset = IR::Instruction::createCmp(
        Opc::ICMP, carryLoad, IR::ConstantInt::get(i32, 0), "eq");
    auto* firstIteration = IR::Instruction::createCmp(
        Opc::ICMP, plan.countLoad,
        IR::ConstantInt::get(i32, plan.initialCount), "eq");
    auto* fixedPoint = IR::Instruction::createBinOp(
        Opc::AND, IR::IntegerType::I1, "fixedpoint.ready",
        carryReset, firstIteration);
    auto* selectedCount = IR::Instruction::createSelect(
        fixedPoint, IR::ConstantInt::get(i32, 1),
        plan.countLoad, "fixedpoint.count");
    position = plan.body->insert(position, carryLoad);
    ++position;
    position = plan.body->insert(position, carryReset);
    ++position;
    position = plan.body->insert(position, firstIteration);
    ++position;
    position = plan.body->insert(position, fixedPoint);
    ++position;
    plan.body->insert(position, selectedCount);
    plan.decrement->setOperand(0, selectedCount);
}

} // namespace

bool dynamicIdempotentLoopElimination(IR::Module* module) {
    if (!module) return false;
    std::vector<FixedPointLoopPlan> plans;
    for (const auto& function : module->getFunctions()) {
        if (function->isExternal()) continue;
        for (const auto& loop : findNaturalLoops(function.get())) {
            FixedPointLoopPlan plan;
            if (matchFixedPointLoop(
                    module, function.get(), loop, plan)) {
                plans.push_back(plan);
            }
        }
    }
    for (const auto& plan : plans) applyFixedPointGuard(plan);
    return !plans.empty();
}

} // namespace Opt
