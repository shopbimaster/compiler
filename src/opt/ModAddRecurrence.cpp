#include "opt/Optimizer.h"

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

struct ModAddLoop {
    IR::Function* function = nullptr;
    IR::BasicBlock* preheader = nullptr;
    IR::BasicBlock* header = nullptr;
    IR::BasicBlock* body = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::Value* inductionPointer = nullptr;
    IR::Value* bound = nullptr;
    IR::Value* boundPointer = nullptr;
    IR::Value* recurrencePointer = nullptr;
    int64_t increment = 0;
    int64_t modulus = 0;
};

IR::ConstantInt* asConstant(IR::Value* value) {
    return dynamic_cast<IR::ConstantInt*>(value);
}

IR::Instruction* asInstruction(
    IR::Value* value, Opc opcode) {
    auto* instruction =
        dynamic_cast<IR::Instruction*>(value);
    return instruction &&
                   instruction->getOpcode() == opcode
        ? instruction
        : nullptr;
}

bool isScalarStorage(IR::Value* pointer) {
    if (!pointer) return false;
    auto* type =
        dynamic_cast<IR::PointerType*>(pointer->getType());
    if (!type ||
        type->getPointeeType() != IR::IntegerType::I32) {
        return false;
    }
    auto* instruction =
        dynamic_cast<IR::Instruction*>(pointer);
    return dynamic_cast<IR::GlobalVariable*>(pointer) ||
           (instruction &&
            instruction->getOpcode() == Opc::ALLOCA);
}

IR::Instruction* getLoadPointer(
    IR::Value* value, IR::Value*& pointer) {
    auto* load = asInstruction(value, Opc::LOAD);
    if (!load || load->getNumOperands() != 1) return nullptr;
    pointer = load->getOperand(0);
    return load;
}

bool matchAddConstant(
    IR::Instruction* add, IR::Value* variable,
    int64_t& constant) {
    if (!add || add->getOpcode() != Opc::ADD ||
        add->getNumOperands() != 2) {
        return false;
    }
    IR::ConstantInt* value = nullptr;
    if (add->getOperand(0) == variable) {
        value = asConstant(add->getOperand(1));
    } else if (add->getOperand(1) == variable) {
        value = asConstant(add->getOperand(0));
    }
    if (!value) return false;
    constant = value->getValue();
    return true;
}

bool blockContainsPhi(IR::BasicBlock* block) {
    for (auto& instruction : block->getInstructions()) {
        if (instruction->getOpcode() == Opc::PHI) return true;
    }
    return false;
}

bool findPreheader(
    const NaturalLoop& loop, const PredMap& predecessors,
    IR::BasicBlock*& preheader) {
    preheader = nullptr;
    auto found = predecessors.find(loop.header);
    if (found == predecessors.end()) return false;
    for (auto* predecessor : found->second) {
        if (loop.body.count(predecessor)) continue;
        if (preheader) return false;
        preheader = predecessor;
    }
    auto* terminator =
        preheader ? preheader->getTerminator() : nullptr;
    return terminator &&
           terminator->getOpcode() == Opc::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == loop.header;
}

bool hasZeroInitialization(
    IR::BasicBlock* preheader, IR::Value* pointer) {
    IR::Instruction* initialization = nullptr;
    for (auto& instruction : preheader->getInstructions()) {
        if (instruction->getOpcode() != Opc::STORE ||
            instruction->getNumOperands() != 2 ||
            instruction->getOperand(1) != pointer) {
            continue;
        }
        initialization = instruction.get();
    }
    auto* zero = initialization
        ? asConstant(initialization->getOperand(0))
        : nullptr;
    return zero && zero->getValue() == 0;
}

bool matchHeader(
    const NaturalLoop& loop,
    IR::BasicBlock*& body,
    IR::BasicBlock*& exit,
    IR::Value*& inductionPointer,
    IR::Value*& bound,
    IR::Value*& boundPointer) {
    auto* terminator = loop.header->getTerminator();
    if (!terminator ||
        terminator->getOpcode() != Opc::COND_BR ||
        terminator->getNumOperands() != 3) {
        return false;
    }
    auto* compare =
        asInstruction(terminator->getOperand(0), Opc::ICMP);
    if (!compare || compare->getName() != "slt" ||
        compare->getNumOperands() != 2) {
        return false;
    }

    IR::Value* inductionLoadPointer = nullptr;
    if (!getLoadPointer(
            compare->getOperand(0),
            inductionLoadPointer) ||
        !isScalarStorage(inductionLoadPointer)) {
        return false;
    }

    auto* trueTarget = dynamic_cast<IR::BasicBlock*>(
        terminator->getOperand(1));
    auto* falseTarget = dynamic_cast<IR::BasicBlock*>(
        terminator->getOperand(2));
    if (!trueTarget || !falseTarget ||
        !loop.body.count(trueTarget) ||
        loop.body.count(falseTarget)) {
        return false;
    }

    IR::Value* loadedBoundPointer = nullptr;
    auto* boundLoad = getLoadPointer(
        compare->getOperand(1), loadedBoundPointer);
    if (boundLoad) {
        if (!isScalarStorage(loadedBoundPointer)) return false;
        bound = nullptr;
        boundPointer = loadedBoundPointer;
    } else {
        auto* boundInstruction =
            dynamic_cast<IR::Instruction*>(
                compare->getOperand(1));
        if (boundInstruction &&
            boundInstruction->getParent() !=
                loop.header->getParent()->getEntryBlock()) {
            return false;
        }
        bound = compare->getOperand(1);
        boundPointer = nullptr;
    }

    body = trueTarget;
    exit = falseTarget;
    inductionPointer = inductionLoadPointer;
    return !blockContainsPhi(loop.header) &&
           !blockContainsPhi(exit);
}

bool matchInductionUpdate(
    IR::BasicBlock* body,
    IR::Value* inductionPointer,
    std::unordered_set<IR::Instruction*>& matched) {
    for (auto& owned : body->getInstructions()) {
        auto* store = owned.get();
        if (store->getOpcode() != Opc::STORE ||
            store->getNumOperands() != 2 ||
            store->getOperand(1) != inductionPointer) {
            continue;
        }
        auto* add = asInstruction(
            store->getOperand(0), Opc::ADD);
        if (!add) return false;
        IR::Value* loadPointer = nullptr;
        IR::Instruction* load = nullptr;
        int64_t step = 0;
        for (unsigned index = 0; index < 2; ++index) {
            auto* candidate = getLoadPointer(
                add->getOperand(index), loadPointer);
            if (candidate &&
                loadPointer == inductionPointer) {
                load = candidate;
                auto* constant =
                    asConstant(add->getOperand(1 - index));
                step = constant ? constant->getValue() : 0;
                break;
            }
        }
        if (!load || step != 1) return false;
        matched.insert(load);
        matched.insert(add);
        matched.insert(store);
        return true;
    }
    return false;
}

bool matchRecurrence(
    IR::BasicBlock* body,
    IR::Value* inductionPointer,
    ModAddLoop& result,
    std::unordered_set<IR::Instruction*>& matched) {
    std::unordered_map<IR::Instruction*, size_t> position;
    size_t current = 0;
    for (auto& instruction : body->getInstructions()) {
        position[instruction.get()] = current++;
    }

    for (auto& owned : body->getInstructions()) {
        auto* finalStore = owned.get();
        if (finalStore->getOpcode() != Opc::STORE ||
            finalStore->getNumOperands() != 2 ||
            finalStore->getOperand(1) == inductionPointer) {
            continue;
        }
        auto* remainder = asInstruction(
            finalStore->getOperand(0), Opc::SREM);
        auto* modulus = remainder &&
                                remainder->getNumOperands() == 2
            ? asConstant(remainder->getOperand(1))
            : nullptr;
        if (!modulus || modulus->getValue() <= 0 ||
            modulus->getValue() >
                std::numeric_limits<int32_t>::max()) {
            continue;
        }

        auto* add = asInstruction(
            remainder->getOperand(0), Opc::ADD);
        IR::Instruction* reload = nullptr;
        IR::Instruction* intermediateStore = nullptr;
        IR::Value* recurrencePointer =
            finalStore->getOperand(1);
        if (!add) {
            IR::Value* reloadPointer = nullptr;
            reload = getLoadPointer(
                remainder->getOperand(0), reloadPointer);
            if (!reload ||
                reloadPointer != recurrencePointer) {
                continue;
            }
            for (auto& candidate : body->getInstructions()) {
                auto* store = candidate.get();
                if (store->getOpcode() != Opc::STORE ||
                    store->getNumOperands() != 2 ||
                    store->getOperand(1) !=
                        recurrencePointer ||
                    position[store] >= position[reload]) {
                    continue;
                }
                auto* candidateAdd = asInstruction(
                    store->getOperand(0), Opc::ADD);
                if (candidateAdd &&
                    (!intermediateStore ||
                     position[store] >
                         position[intermediateStore])) {
                    intermediateStore = store;
                    add = candidateAdd;
                }
            }
        }
        if (!add || !isScalarStorage(recurrencePointer) ||
            recurrencePointer == inductionPointer) {
            continue;
        }

        IR::Instruction* recurrenceLoad = nullptr;
        int64_t increment = 0;
        for (unsigned index = 0; index < 2; ++index) {
            IR::Value* pointer = nullptr;
            auto* candidate = getLoadPointer(
                add->getOperand(index), pointer);
            auto* constant =
                asConstant(add->getOperand(1 - index));
            if (candidate && pointer == recurrencePointer &&
                constant) {
                recurrenceLoad = candidate;
                increment = constant->getValue();
                break;
            }
        }
        if (!recurrenceLoad || increment <= 0 ||
            increment >
                std::numeric_limits<int32_t>::max()) {
            continue;
        }
        if (position[recurrenceLoad] >= position[add] ||
            position[add] >= position[remainder] ||
            position[remainder] >= position[finalStore]) {
            continue;
        }
        if (reload &&
            (!intermediateStore ||
             position[add] >= position[intermediateStore] ||
             position[intermediateStore] >= position[reload] ||
             position[reload] >= position[remainder])) {
            continue;
        }

        result.recurrencePointer = recurrencePointer;
        result.increment = increment;
        result.modulus = modulus->getValue();
        matched.insert(recurrenceLoad);
        matched.insert(add);
        if (intermediateStore) {
            matched.insert(intermediateStore);
            matched.insert(reload);
        }
        matched.insert(remainder);
        matched.insert(finalStore);
        return true;
    }
    return false;
}

bool matchLoop(
    IR::Function* function, const NaturalLoop& loop,
    const PredMap& predecessors, ModAddLoop& result) {
    if (!function || loop.body.size() != 2 ||
        loop.latch == loop.header) {
        return false;
    }
    IR::BasicBlock* preheader = nullptr;
    if (!findPreheader(loop, predecessors, preheader)) {
        return false;
    }

    IR::BasicBlock* body = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::Value* inductionPointer = nullptr;
    IR::Value* bound = nullptr;
    IR::Value* boundPointer = nullptr;
    if (!matchHeader(
            loop, body, exit, inductionPointer,
            bound, boundPointer) ||
        body != loop.latch ||
        !hasZeroInitialization(
            preheader, inductionPointer)) {
        return false;
    }
    if (boundPointer == inductionPointer) return false;

    auto* bodyTerminator = body->getTerminator();
    if (!bodyTerminator ||
        bodyTerminator->getOpcode() != Opc::BR ||
        bodyTerminator->getNumOperands() != 1 ||
        bodyTerminator->getOperand(0) != loop.header) {
        return false;
    }

    result.function = function;
    result.preheader = preheader;
    result.header = loop.header;
    result.body = body;
    result.exit = exit;
    result.inductionPointer = inductionPointer;
    result.bound = bound;
    result.boundPointer = boundPointer;

    std::unordered_set<IR::Instruction*> matched = {
        bodyTerminator};
    if (!matchInductionUpdate(
            body, inductionPointer, matched) ||
        !matchRecurrence(
            body, inductionPointer, result, matched) ||
        result.recurrencePointer == boundPointer) {
        return false;
    }
    for (auto& instruction : body->getInstructions()) {
        if (!matched.count(instruction.get())) return false;
    }
    return true;
}

IR::BasicBlock::iterator findTerminator(
    IR::BasicBlock* block) {
    auto* terminator = block->getTerminator();
    for (auto iterator = block->begin();
         iterator != block->end(); ++iterator) {
        if (iterator->get() == terminator) return iterator;
    }
    return block->end();
}

bool applyLoopVersioning(const ModAddLoop& loop) {
    auto insertion = findTerminator(loop.preheader);
    if (insertion == loop.preheader->end()) return false;

    auto insert = [&](IR::Instruction* instruction) {
        insertion =
            loop.preheader->insert(insertion, instruction);
        ++insertion;
        return instruction;
    };

    auto* i32 = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* intMax = IR::ConstantInt::get(
        i32, std::numeric_limits<int32_t>::max());
    auto* increment =
        IR::ConstantInt::get(i32, loop.increment);
    auto* modulus =
        IR::ConstantInt::get(i32, loop.modulus);

    IR::Value* tripCount = loop.bound;
    if (loop.boundPointer) {
        tripCount = insert(IR::Instruction::createLoad(
            i32, loop.boundPointer, "modrec.trip"));
    }
    auto* initial = insert(IR::Instruction::createLoad(
        i32, loop.recurrencePointer, "modrec.initial"));
    auto* positiveTrip = insert(IR::Instruction::createCmp(
        Opc::ICMP, tripCount, zero, "sgt"));
    auto* nonnegativeInitial =
        insert(IR::Instruction::createCmp(
            Opc::ICMP, initial, zero, "sge"));
    auto* remaining = insert(
        IR::Instruction::createBinOp(
            Opc::SUB, i32, "modrec.remaining",
            intMax, initial));
    auto* maxTrips = insert(
        IR::Instruction::createBinOp(
            Opc::SDIV, i32, "modrec.max_trips",
            remaining, increment));
    auto* tripFits = insert(IR::Instruction::createCmp(
        Opc::ICMP, tripCount, maxTrips, "sle"));
    auto* nonnegativeAndFits = insert(
        IR::Instruction::createBinOp(
            Opc::AND, IR::IntegerType::I1,
            "modrec.nonnegative_fits",
            nonnegativeInitial, tripFits));
    auto* safe = insert(IR::Instruction::createBinOp(
        Opc::AND, IR::IntegerType::I1, "modrec.safe",
        positiveTrip, nonnegativeAndFits));
    auto* scaled = insert(IR::Instruction::createBinOp(
        Opc::MUL, i32, "modrec.scaled",
        tripCount, increment));
    auto* sum = insert(IR::Instruction::createBinOp(
        Opc::ADD, i32, "modrec.sum", initial, scaled));
    auto* closed = insert(IR::Instruction::createBinOp(
        Opc::SREM, i32, "modrec.closed", sum, modulus));

    auto* fast = loop.function->createBlock(
        "modrec.fast");
    fast->pushBack(IR::Instruction::createStore(
        closed, loop.recurrencePointer));
    fast->pushBack(IR::Instruction::createStore(
        tripCount, loop.inductionPointer));
    fast->pushBack(IR::Instruction::createBr(loop.exit));

    insertion = loop.preheader->erase(insertion);
    loop.preheader->insert(
        insertion,
        IR::Instruction::createCondBr(
            safe, fast, loop.header));
    return true;
}

} // namespace

bool modAddRecurrenceStrengthReduce(IR::Module* module) {
    bool changed = false;
    for (auto& ownedFunction : module->getFunctions()) {
        auto* function = ownedFunction.get();
        if (!function || function->isExternal()) continue;

        auto loops = findNaturalLoops(function);
        auto predecessors = buildPredecessors(function);
        std::vector<ModAddLoop> candidates;
        for (const auto& loop : loops) {
            ModAddLoop candidate;
            if (matchLoop(
                    function, loop, predecessors, candidate)) {
                candidates.push_back(candidate);
            }
        }
        for (const auto& candidate : candidates) {
            if (applyLoopVersioning(candidate)) {
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace Opt
