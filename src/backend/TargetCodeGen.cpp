#include "backend/TargetCodeGen.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>

namespace Backend {

static const std::unordered_map<Register, const char*> REG_NAMES = {
    {Register::x0, "x0"},  {Register::ra, "ra"},  {Register::sp, "sp"},
    {Register::gp, "gp"},  {Register::tp, "tp"},  {Register::t0, "t0"},
    {Register::t1, "t1"},  {Register::t2, "t2"},  {Register::s0, "s0"},
    {Register::s1, "s1"},  {Register::a0, "a0"},  {Register::a1, "a1"},
    {Register::a2, "a2"},  {Register::a3, "a3"},  {Register::a4, "a4"},
    {Register::a5, "a5"},  {Register::a6, "a6"},  {Register::a7, "a7"},
    {Register::s2, "s2"},  {Register::s3, "s3"},  {Register::s4, "s4"},
    {Register::s5, "s5"},  {Register::s6, "s6"},  {Register::s7, "s7"},
    {Register::s8, "s8"},  {Register::s9, "s9"},  {Register::s10, "s10"},
    {Register::s11, "s11"}, {Register::t3, "t3"}, {Register::t4, "t4"},
    {Register::t5, "t5"},  {Register::t6, "t6"},
};

std::string regToString(Register reg) {
    auto it = REG_NAMES.find(reg);
    return it != REG_NAMES.end() ? std::string(it->second) : "?";
}

TargetCodeGen::TargetCodeGen()
    : stackSize(0), labelCounter(0) {}

std::string TargetCodeGen::generate(IR::Module& module) {
    emitter = CodeEmitter();
    labelCounter = 0;

    emitter.emitText("  .text");

    auto& globals = module.getGlobals();
    for (auto& gv : globals) {
        emitGlobal(gv.get());
    }

    if (!globals.empty()) {
        emitter.emitText("");
    }

    bool firstFunc = true;
    for (auto& func : module.getFunctions()) {
        if (func->isExternal()) continue;
        if (!firstFunc) emitter.emitText("");
        firstFunc = false;
        emitFunction(*func);
    }

    std::string result;
    std::string data = emitter.getDataSection();
    if (!data.empty()) {
        result += data + "\n";
    }
    result += emitter.getTextSection();
    return result;
}

void TargetCodeGen::emitGlobal(IR::GlobalVariable* gv) {
    IR::Type* valType = gv->getType();
    auto* ptrTy = dynamic_cast<IR::PointerType*>(valType);
    if (!ptrTy) return;
    IR::Type* pointee = ptrTy->getPointeeType();

    std::string section;
    if (gv->isConstant()) {
        section = ".section .rodata";
    } else if (gv->getInitializer()) {
        section = ".data";
    } else {
        section = ".bss";
    }
    emitter.emitData("");
    emitter.emitData("  " + section);

    emitter.emitData("  .globl  " + gv->getName());
    emitter.emitData("  .type   " + gv->getName() + ", @object");

    if (pointee->isArray()) {
        auto* arrTy = dynamic_cast<IR::ArrayType*>(pointee);
        int totalSize = getTypeSize(arrTy);

        if (gv->getInitializer()) {
            emitter.emitData("  .align  2");
            emitter.emitData("  .size   " + gv->getName() + ", " + std::to_string(totalSize));
            emitter.emitData(gv->getName() + ":");

            // Check for flat init data (aggregate initializer)
            const auto& initData = gv->getInitData();
            if (!initData.empty()) {
                for (uint32_t word : initData) {
                    emitter.emitData("  .word   " + std::to_string(static_cast<int32_t>(word)));
                }
            } else {
                emitGlobalInitData(gv->getInitializer(), pointee, "  ");
            }
        } else {
            emitter.emitData("  .align  2");
            emitter.emitData("  .size   " + gv->getName() + ", " + std::to_string(totalSize));
            emitter.emitData(gv->getName() + ":");
            emitter.emitData("  .zero   " + std::to_string(totalSize));
        }
    } else if (pointee->isInteger() || pointee->isFloat()) {
        emitter.emitData("  .align  2");
        emitter.emitData("  .size   " + gv->getName() + ", 4");
        emitter.emitData(gv->getName() + ":");

        if (gv->getInitializer()) {
            if (pointee->isFloat()) {
                union { float f; uint32_t i; } u;
                if (auto* ci = dynamic_cast<IR::ConstantInt*>(gv->getInitializer())) {
                    u.f = static_cast<float>(ci->getValue());
                } else if (auto* cf = dynamic_cast<IR::ConstantFloat*>(gv->getInitializer())) {
                    u.f = static_cast<float>(cf->getValue());
                } else {
                    u.f = 0.0f;
                }
                emitter.emitData("  .word   " + std::to_string(u.i));
            } else {
                if (auto* ci = dynamic_cast<IR::ConstantInt*>(gv->getInitializer())) {
                    emitter.emitData("  .word   " + std::to_string(ci->getValue()));
                } else if (auto* cf = dynamic_cast<IR::ConstantFloat*>(gv->getInitializer())) {
                    emitter.emitData("  .word   " + std::to_string(static_cast<int>(cf->getValue())));
                } else {
                    emitter.emitData("  .word   0");
                }
            }
        } else {
            emitter.emitData("  .word   0");
        }
    }
}

void TargetCodeGen::emitGlobalInitData(IR::Constant* init, IR::Type* type, const std::string& indent) {
    if (auto* ci = dynamic_cast<IR::ConstantInt*>(init)) {
        emitter.emitData(indent + ".word   " + std::to_string(ci->getValue()));
        return;
    }
    if (auto* cf = dynamic_cast<IR::ConstantFloat*>(init)) {
        union { float f; uint32_t i; } u;
        u.f = static_cast<float>(cf->getValue());
        emitter.emitData(indent + ".word   " + std::to_string(u.i));
        return;
    }
    emitter.emitData(indent + ".word   0");
}

void TargetCodeGen::emitFunction(IR::Function& func) {
    currentFunc = &func;
    promotedAllocas.clear();
    regAlloc.clearReservedRegs();

    // 先收集全局变量地址并分配 callee-saved 寄存器缓存（优先级高于 ALLOCA 提升）
    globalAddrCache.clear();
    collectGlobalAddresses(func);
    for (auto& [gv, reg] : globalAddrCache) {
        regAlloc.reserveReg(reg);
    }

    // 再提升 ALLOCA 到寄存器（跳过已被全局地址缓存占用的寄存器）
    promoteAllocasInFunction(func);

    computeStackLayout(func);

    // 预留被提升的寄存器，防止寄存器分配器使用它们
    for (auto& [alloca, reg] : promotedAllocas) {
        regAlloc.reserveReg(reg);
    }

    regAlloc.allocate(func);

    // 收集可以融合的 GEP+LOAD/STORE 模式（FoldMemoryAccess）
    foldedGeps.clear();
    collectFoldedGeps(func);

    // 将 promoted ALLOCA 的 LOAD 指令映射到 callee-saved 寄存器，
    // 避免后续 emitLoad 生成冗余的 mv 指令
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                auto* ptrOp = inst->getOperand(0);
                auto promotedIt = promotedAllocas.find(ptrOp);
                if (promotedIt != promotedAllocas.end()) {
                    regAlloc.setReg(inst.get(), promotedIt->second);
                }
            }
        }
    }

    int savedRegCount = static_cast<int>(regAlloc.getUsedCalleeSaved().size());
    int savedRegSpace = savedRegCount * 8;
    stackSize += savedRegSpace;
    stackSize = (stackSize + 15) & ~15;

    emitter.emitText("  .globl  " + func.getName());
    emitter.emitText("  .type   " + func.getName() + ", @function");
    emitter.emitText(func.getName() + ":");

    emitPrologue(func);

    for (auto& bb : func.getBlocks()) {
        emitBasicBlock(*bb);
    }

    for (auto& bb : func.getBlocks()) {
        (void)bb;
    }

    emitter.emitText(func.getName() + "_exit:");
    emitEpilogue(func);

    currentFunc = nullptr;
    vregStackOffset.clear();
    allocaOffset.clear();
}

void TargetCodeGen::computeStackLayout(IR::Function& func) {
    // Reserve space for outgoing stack arguments (beyond register limits).
    // RISC-V convention: up to 8 integer params (a0-a7) and 8 float params (fa0-fa7).
    int maxStackArgs = 0;
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                int numArgs = static_cast<int>(inst->getNumOperands()) - 1;
                int intRegCount = 0, floatRegCount = 0;
                for (int j = 0; j < numArgs; ++j) {
                    auto* argTy = inst->getOperand(j + 1)->getType();
                    if (argTy && argTy->isFloat()) {
                        if (floatRegCount < 8) floatRegCount++;
                    } else {
                        if (intRegCount < 8) intRegCount++;
                    }
                }
                int regParams = intRegCount + floatRegCount;
                int stackArgs = numArgs - regParams;
                if (stackArgs > maxStackArgs) {
                    maxStackArgs = stackArgs;
                }
            }
        }
    }
    stackSize = maxStackArgs * 8;

    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::ALLOCA) {
                // 跳过已提升到寄存器的 ALLOCA
                if (promotedAllocas.find(inst.get()) != promotedAllocas.end()) continue;
                allocaOffset[inst.get()] = stackSize;
                if (auto* ptrTy = dynamic_cast<IR::PointerType*>(inst->getType())) {
                    int typeSize = getTypeSize(ptrTy->getPointeeType());
                    stackSize += ((typeSize + 7) / 8) * 8;
                } else {
                    stackSize += 8;
                }
            }
        }
    }

    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            IR::Value* vr = inst.get();
            if (vregStackOffset.find(vr) != vregStackOffset.end()) continue;

            unsigned opCount = inst->getNumOperands();
            for (unsigned i = 0; i < opCount; ++i) {
                IR::Value* opVal = inst->getOperand(i);
                if (opVal && !dynamic_cast<IR::Constant*>(opVal)
                    && !dynamic_cast<IR::BasicBlock*>(opVal)
                    && !dynamic_cast<IR::Function*>(opVal)
                    && allocaOffset.find(opVal) == allocaOffset.end()
                    && vregStackOffset.find(opVal) == vregStackOffset.end()) {
                    vregStackOffset[opVal] = stackSize;
                    stackSize += 8;
                }
            }

            if (inst->getOpcode() != IR::Instruction::Opcode::RET
                && inst->getOpcode() != IR::Instruction::Opcode::BR
                && inst->getOpcode() != IR::Instruction::Opcode::COND_BR
                && inst->getOpcode() != IR::Instruction::Opcode::STORE
                && inst->getOpcode() != IR::Instruction::Opcode::ALLOCA
                && inst->getType() && !inst->getType()->isVoid()) {
                vregStackOffset[vr] = stackSize;
                stackSize += 8;
            }
        }
    }

    for (unsigned i = 0; i < func.getNumArgs(); ++i) {
        auto* arg = func.getArg(i);
        paramOffsets[arg] = stackSize;
        stackSize += 8;
    }

    bool savesRA = false;
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                savesRA = true;
                break;
            }
        }
        if (savesRA) break;
    }
    if (savesRA || stackSize > 0) {
        stackSize += 8;
    }

    stackSize = (stackSize + 15) & ~15;
}

int TargetCodeGen::getTypeSize(IR::Type* t) {
    if (t->isInteger()) return 4;
    if (t->isFloat()) return 4;
    if (t->isPointer()) return 8;
    if (t->isArray()) {
        auto* at = dynamic_cast<IR::ArrayType*>(t);
        return at->getNumElements() * getTypeSize(at->getElementType());
    }
    return 8;
}

// Helper: adjust sp by delta (handles large immediates)
std::string TargetCodeGen::emitSPAddImm(int delta) {
    std::string result;
    if (delta == 0) return result;
    if (fitsImm12(delta)) {
        result += "  addi    sp, sp, " + std::to_string(delta) + "\n";
    } else {
        int absDelta = (delta < 0) ? -delta : delta;
        result += "  li      t0, " + std::to_string(absDelta) + "\n";
        result += (delta < 0) ? "  sub     sp, sp, t0\n" : "  add     sp, sp, t0\n";
    }
    return result;
}

// Helper: load from sp+offset to reg, handling large offsets
std::string TargetCodeGen::emitStackLoad(const std::string& reg, int offset, const std::string& insn) {
    std::string result;
    if (fitsImm12(offset)) {
        result += "  " + insn + "      " + reg + ", " + std::to_string(offset) + "(sp)\n";
    } else {
        result += "  li      t2, " + std::to_string(offset) + "\n";
        result += "  add     t2, sp, t2\n";
        result += "  " + insn + "      " + reg + ", 0(t2)\n";
    }
    return result;
}

// Helper: store reg to sp+offset, handling large offsets
std::string TargetCodeGen::emitStackStore(const std::string& reg, int offset, const std::string& insn) {
    std::string result;
    if (fitsImm12(offset)) {
        result += "  " + insn + "      " + reg + ", " + std::to_string(offset) + "(sp)\n";
    } else {
        result += "  li      t2, " + std::to_string(offset) + "\n";
        result += "  add     t2, sp, t2\n";
        result += "  " + insn + "      " + reg + ", 0(t2)\n";
    }
    return result;
}

void TargetCodeGen::emitPrologue(IR::Function& func) {
    // Adjust stack pointer, handling large stack frames
    emitter.emitText(emitSPAddImm(-stackSize));

    if (stackSize > 0) {
        emitter.emitText(emitStackStore("ra", stackSize - 8, "sd"));
    }

    int csrOffset = stackSize - 8;
    for (auto& reg : regAlloc.getUsedCalleeSaved()) {
        csrOffset -= 8;
        if (reg[0] == 'f') {
            emitter.emitText(emitStackStore(reg, csrOffset, "fsd"));
        } else {
            emitter.emitText(emitStackStore(reg, csrOffset, "sd"));
        }
    }

    auto* ft = func.getFunctionType();
    unsigned iReg = 0;  // Next integer argument register
    unsigned fReg = 0;  // Next float argument register
    unsigned stackParamIdx = 0;  // Stack parameter counter
    for (unsigned i = 0; i < func.getNumArgs(); ++i) {
        auto* arg = func.getArg(i);
        int offset = getStackOffset(arg);
        bool pIsFloat = ft->getParamTypes()[i]->isFloat();
        bool pIsPtr = ft->getParamTypes()[i]->isPointer();

        if (pIsFloat && fReg < 8) {
            std::string reg = std::string("fa") + std::to_string(fReg++);
            emitter.emitText(emitStackStore(reg, offset, "fsw"));
        } else if (!pIsFloat && iReg < 8) {
            std::string reg = std::string("a") + std::to_string(iReg++);
            emitter.emitText(emitStackStore(reg, offset, pIsPtr ? "sd" : "sw"));
        } else {
            // Arguments beyond registers: load from the caller's stack frame
            int callerOffset = stackSize + stackParamIdx * 8;
            stackParamIdx++;
            if (pIsFloat) {
                emitter.emitText(emitStackLoad("ft0", callerOffset, "flw"));
                emitter.emitText(emitStackStore("ft0", offset, "fsw"));
            } else if (pIsPtr) {
                emitter.emitText(emitStackLoad("t1", callerOffset, "ld"));
                emitter.emitText(emitStackStore("t1", offset, "sd"));
            } else {
                emitter.emitText(emitStackLoad("t1", callerOffset, "lw"));
                emitter.emitText(emitStackStore("t1", offset, "sw"));
            }
        }
    }

    for (unsigned i = 0; i < func.getNumArgs(); ++i) {
        auto* arg = func.getArg(i);
        if (regAlloc.hasReg(arg)) {
            int offset = getStackOffset(arg);
            std::string r = regAlloc.getReg(arg);
            auto* pt = ft->getParamTypes()[i];
            bool paramFloat = pt->isFloat();
            bool regFloat = !r.empty() && r[0] == 'f';

            if (paramFloat && regFloat) {
                emitter.emitText(emitStackLoad(r, offset, "flw"));
            } else if (paramFloat && !regFloat) {
                emitter.emitText(emitStackLoad("ft0", offset, "flw"));
                emitter.emitText("  fmv.x.w " + r + ", ft0\n");
            } else if (!paramFloat && regFloat) {
                emitter.emitText(emitStackLoad("t2", offset,
                    pt->isPointer() ? "ld" : "lw"));
                emitter.emitText("  " + std::string(pt->isPointer() ? "fmv.d.x" : "fmv.w.x") + " " + r + ", t2\n");
            } else {
                emitter.emitText(emitStackLoad(r, offset,
                    pt->isPointer() ? "ld" : "lw"));
            }
        }
    }

    // 初始化全局变量地址缓存
    for (auto& [gv, reg] : globalAddrCache) {
        emitter.emitText("  la      " + reg + ", " + gv->getName());
    }
}

void TargetCodeGen::emitEpilogue(IR::Function& func) {
    if (stackSize > 0) {
        int csrOffset = stackSize - 8;
        for (auto& reg : regAlloc.getUsedCalleeSaved()) {
            csrOffset -= 8;
            if (reg[0] == 'f') {
                emitter.emitText(emitStackLoad(reg, csrOffset, "fld"));
            } else {
                emitter.emitText(emitStackLoad(reg, csrOffset, "ld"));
            }
        }
        emitter.emitText(emitStackLoad("ra", stackSize - 8, "ld"));
        emitter.emitText(emitSPAddImm(stackSize));
    }
    emitter.emitText("  ret");
}

// ================================================================
// FoldMemoryAccess: 收集 GEP 指令，其唯一使用者是 LOAD 或 STORE
// 这些 GEP 的地址计算将被内联到 LOAD/STORE 中，避免
// 存储→加载 GEP 结果的往返开销
// ================================================================
void TargetCodeGen::collectFoldedGeps(IR::Function& func) {
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::GETELEMENTPTR)
                continue;
            // 仅当 GEP 恰好有一个使用者时才可融合
            if (!inst->hasOneUse())
                continue;
            auto& uses = inst->getUses();
            auto* user = dynamic_cast<IR::Instruction*>(uses[0].user);
            if (!user)
                continue;
            auto userOp = user->getOpcode();
            if (userOp != IR::Instruction::Opcode::LOAD &&
                userOp != IR::Instruction::Opcode::STORE)
                continue;

            // 安全检查：如果 GEP 的基指针（operand 0）本身也是一个 GEP 指令，
            // 则跳过融合。嵌套 GEP 融合会导致复杂的活跃范围交互，
            // 可能使寄存器分配器错误地复用寄存器。
            auto* basePtr = inst->getOperand(0);
            if (auto* baseInst = dynamic_cast<IR::Instruction*>(basePtr)) {
                if (baseInst->getOpcode() == IR::Instruction::Opcode::GETELEMENTPTR)
                    continue;
            }

            foldedGeps.insert(inst.get());
        }
    }
}

// 将 GEP 的地址计算内联到指定寄存器中，不存储结果
// 使用 addrReg 作为基址/结果寄存器，t1 作为索引寄存器，t3 作为临时乘法寄存器
std::string TargetCodeGen::emitGEPAddressToReg(IR::Instruction& gep,
                                                const std::string& addrReg) {
    std::string code;
    code += loadToReg(gep.getOperand(0), addrReg);

    unsigned numOps = gep.getNumOperands();
    if (numOps >= 2) {
        auto* ptrTy = dynamic_cast<IR::PointerType*>(gep.getOperand(0)->getType());
        IR::Type* curPointee = ptrTy ? ptrTy->getPointeeType() : nullptr;

        // 第一个索引（operand 1）：指针偏移，乘以 sizeof(pointee)
        auto* firstIdx = dynamic_cast<IR::ConstantInt*>(gep.getOperand(1));
        bool firstIsZero = firstIdx && firstIdx->getValue() == 0;
        if (!firstIsZero) {
            int ptrStride = curPointee ? getTypeSize(curPointee) : 4;
            code += loadToReg(gep.getOperand(1), "t1");
            if (ptrStride == 1) {
            } else if (ptrStride == 2) {
                code += "  slli    t1, t1, 1\n";
            } else if (ptrStride == 4) {
                code += "  slli    t1, t1, 2\n";
            } else if (ptrStride == 8) {
                code += "  slli    t1, t1, 3\n";
            } else {
                code += "  li      t3, " + std::to_string(ptrStride) + "\n";
                code += "  mul     t1, t1, t3\n";
            }
            code += "  add     " + addrReg + ", " + addrReg + ", t1\n";
        }

        // 后续索引（operand 2+）：数组索引
        for (unsigned i = 2; i < numOps; ++i) {
            auto* idxConst = dynamic_cast<IR::ConstantInt*>(gep.getOperand(i));
            if (idxConst && idxConst->getValue() == 0) {
                if (curPointee && curPointee->isArray()) {
                    auto* arrTy = dynamic_cast<IR::ArrayType*>(curPointee);
                    curPointee = arrTy->getElementType();
                }
                continue;
            }

            int stride = 4;
            if (curPointee && curPointee->isArray()) {
                auto* arrTy = dynamic_cast<IR::ArrayType*>(curPointee);
                stride = getTypeSize(arrTy->getElementType());
                curPointee = arrTy->getElementType();
            }
            code += loadToReg(gep.getOperand(i), "t1");
            if (stride == 1) {
            } else if (stride == 2) {
                code += "  slli    t1, t1, 1\n";
            } else if (stride == 4) {
                code += "  slli    t1, t1, 2\n";
            } else if (stride == 8) {
                code += "  slli    t1, t1, 3\n";
            } else {
                code += "  li      t3, " + std::to_string(stride) + "\n";
                code += "  mul     t1, t1, t3\n";
            }
            code += "  add     " + addrReg + ", " + addrReg + ", t1\n";
        }
    }
    return code;
}

void TargetCodeGen::emitBasicBlock(IR::BasicBlock& bb) {
    // Use .L prefix for local labels to avoid symbol conflicts across functions
    emitter.emitText(".L" + currentFunc->getName() + "_" + bb.getName() + ":");
    for (auto& inst : bb.getInstructions()) {
        emitInstruction(*inst);
    }
}

void TargetCodeGen::emitInstruction(IR::Instruction& inst) {
    using Opc = IR::Instruction::Opcode;

    switch (inst.getOpcode()) {
    case Opc::RET:
        emitRet(inst);
        break;
    case Opc::BR:
        emitBr(inst);
        break;
    case Opc::COND_BR:
        emitCondBr(inst);
        break;
    case Opc::ADD:
    case Opc::SUB:
    case Opc::MUL:
    case Opc::SDIV:
    case Opc::SREM:
    case Opc::AND:
    case Opc::OR:
    case Opc::XOR:
    case Opc::SHL:
    case Opc::ASHR:
    case Opc::SMULH:
        emitBinOp(inst);
        break;
    case Opc::ICMP:
        emitIcmp(inst);
        break;
    case Opc::ALLOCA:
        break;
    case Opc::LOAD:
        emitLoad(inst);
        break;
    case Opc::STORE:
        emitStore(inst);
        break;
    case Opc::CALL:
        emitCall(inst);
        break;
    case Opc::GETELEMENTPTR:
        emitGetElementPtr(inst);
        break;
    case Opc::ZEXT:
    case Opc::SEXT:
        break;
    case Opc::SITOFP:
        emitSitofp(inst);
        break;
    case Opc::FPTOSI:
        emitFptosi(inst);
        break;
    case Opc::FADD:
    case Opc::FSUB:
    case Opc::FMUL:
    case Opc::FDIV:
        emitFBinOp(inst);
        break;
    case Opc::FCMP:
        emitFcmp(inst);
        break;
    case Opc::SELECT:
        emitSelect(inst);
        break;
    default:
        emitter.emitText("  # unknown opcode " + std::to_string(static_cast<int>(inst.getOpcode())));
        break;
    }
}

static bool isFloatReg(const std::string& reg) {
    return !reg.empty() && reg[0] == 'f';
}

std::string TargetCodeGen::loadToReg(IR::Value* val, const std::string& destReg) {
    std::string result;
    bool destIsFloat = isFloatReg(destReg);

    if (auto* ci = dynamic_cast<IR::ConstantInt*>(val)) {
        if (!destIsFloat) {
            result += "  li      " + destReg + ", " + std::to_string(ci->getValue()) + "\n";
        } else {
            result += "  li      t2, " + std::to_string(ci->getValue()) + "\n";
            result += "  fcvt.s.w " + destReg + ", t2\n";
        }
        return result;
    }
    if (auto* cf = dynamic_cast<IR::ConstantFloat*>(val)) {
        union { float f; uint32_t i; } u;
        u.f = static_cast<float>(cf->getValue());
        if (!destIsFloat) {
            result += "  li      " + destReg + ", " + std::to_string(u.i) + "\n";
        } else {
            result += "  li      t2, " + std::to_string(u.i) + "\n";
            result += "  fmv.w.x " + destReg + ", t2\n";
        }
        return result;
    }

    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(val)) {
        auto it = globalAddrCache.find(gv);
        if (it != globalAddrCache.end()) {
            // 已缓存，直接从缓存寄存器获取
            result += "  mv      " + destReg + ", " + it->second + "\n";
        } else {
            result += "  la      " + destReg + ", " + gv->getName() + "\n";
        }
        return result;
    }

    int offset = getStackOffset(val);

    if (allocaOffset.find(val) != allocaOffset.end()) {
        if (offset != 0) {
            if (fitsImm12(offset)) {
                result += "  addi    " + destReg + ", sp, " + std::to_string(offset) + "\n";
            } else {
                result += "  li      t1, " + std::to_string(offset) + "\n";
                result += "  add     " + destReg + ", sp, t1\n";
            }
        } else {
            result += "  mv      " + destReg + ", sp\n";
        }
        return result;
    }

    if (regAlloc.hasReg(val)) {
        std::string r = regAlloc.getReg(val);
        if (r != destReg) {
            bool srcFloat = isFloatReg(r);
            bool dstFloat = isFloatReg(destReg);
            auto* valTy = val->getType();
            bool isPtr = valTy && valTy->isPointer();
            if (srcFloat && dstFloat) {
                result += "  fmv.s   " + destReg + ", " + r + "\n";
            } else if (srcFloat && !dstFloat) {
                result += "  " + std::string(isPtr ? "fmv.x.d" : "fmv.x.w") + " " + destReg + ", " + r + "\n";
            } else if (!srcFloat && dstFloat) {
                result += "  " + std::string(isPtr ? "fmv.d.x" : "fmv.w.x") + " " + destReg + ", " + r + "\n";
            } else {
                result += "  mv      " + destReg + ", " + r + "\n";
            }
        }
        return result;
    }

    auto* ty = val->getType();
    bool valIsFloat = ty && ty->isFloat();
    bool valIsPtr = ty && ty->isPointer();

    if (valIsFloat && destIsFloat) {
        result += emitStackLoad(destReg, offset, "flw");
    } else if (valIsFloat && !destIsFloat) {
        // Float value loaded into int register: use float temp then convert
        result += emitStackLoad("ft0", offset, "flw");
        result += "  fmv.x.w " + destReg + ", ft0\n";
    } else if (!valIsFloat && destIsFloat) {
        // Int/pointer value loaded into float register: use int temp then convert
        result += emitStackLoad("t2", offset, valIsPtr ? "ld" : "lw");
        result += "  " + std::string(valIsPtr ? "fmv.d.x" : "fmv.w.x") + " " + destReg + ", t2\n";
    } else {
        result += emitStackLoad(destReg, offset, valIsPtr ? "ld" : "lw");
    }
    return result;
}

std::string TargetCodeGen::storeFromReg(IR::Value* val, const std::string& srcReg, bool addNew) {
    (void)addNew;
    std::string result;

    if (regAlloc.hasReg(val)) {
        std::string r = regAlloc.getReg(val);
        if (r != srcReg) {
            bool srcFloat = isFloatReg(srcReg);
            bool dstFloat = isFloatReg(r);
            auto* valTy = val->getType();
            bool isPtr = valTy && valTy->isPointer();
            if (srcFloat && dstFloat) {
                result += "  fmv.s   " + r + ", " + srcReg + "\n";
            } else if (srcFloat && !dstFloat) {
                result += "  " + std::string(isPtr ? "fmv.x.d" : "fmv.x.w") + " " + r + ", " + srcReg + "\n";
            } else if (!srcFloat && dstFloat) {
                result += "  " + std::string(isPtr ? "fmv.d.x" : "fmv.w.x") + " " + r + ", " + srcReg + "\n";
            } else {
                result += "  mv      " + r + ", " + srcReg + "\n";
            }
        }
        // Value is safe in callee-saved register, skip stack store
        return result;
    }

    int offset = getStackOffset(val);
    auto* ty = val->getType();
    bool valIsFloat = ty && ty->isFloat();
    bool srcIsFloat = !srcReg.empty() && srcReg[0] == 'f';

    if (valIsFloat && srcIsFloat) {
        result += emitStackStore(srcReg, offset, "fsw");
    } else if (valIsFloat && !srcIsFloat) {
        result += "  fmv.w.x ft0, " + srcReg + "\n";
        result += emitStackStore("ft0", offset, "fsw");
    } else if (!valIsFloat && srcIsFloat) {
        result += "  " + std::string((ty && ty->isPointer()) ? "fmv.x.d" : "fmv.x.w") + " t2, " + srcReg + "\n";
        result += emitStackStore("t2", offset,
            (ty && ty->isPointer()) ? "sd" : "sw");
    } else {
        result += emitStackStore(srcReg, offset,
            (ty && ty->isPointer()) ? "sd" : "sw");
    }
    return result;
}

int TargetCodeGen::allocSlot(IR::Value* val) {
    if (vregStackOffset.find(val) != vregStackOffset.end()) {
        return vregStackOffset[val];
    }
    int offset = stackSize;
    vregStackOffset[val] = offset;
    stackSize += 16;
    return offset;
}

int TargetCodeGen::getStackOffset(IR::Value* val) {
    if (auto* arg = dynamic_cast<IR::Argument*>(val)) {
        auto it = paramOffsets.find(arg);
        return it != paramOffsets.end() ? it->second : 0;
    }

    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(val)) {
        return -1;
    }

    auto it = vregStackOffset.find(val);
    if (it != vregStackOffset.end()) return it->second;

    auto ait = allocaOffset.find(val);
    if (ait != allocaOffset.end()) return ait->second;

    return 0;
}

std::string TargetCodeGen::emitGlobalAddr(IR::GlobalVariable* gv, const std::string& destReg) {
    std::string result;
    result += "  la      " + destReg + ", " + gv->getName() + "\n";
    return result;
}

std::string TargetCodeGen::emitValueToReg(IR::Value* val, const std::string& destReg) {
    if (auto* ci = dynamic_cast<IR::ConstantInt*>(val)) {
        bool destIsFloat = isFloatReg(destReg);
        std::string result;
        if (destIsFloat) {
            result += "  li      t2, " + std::to_string(ci->getValue()) + "\n";
            result += "  fcvt.s.w " + destReg + ", t2\n";
        } else {
            result += "  li      " + destReg + ", " + std::to_string(ci->getValue()) + "\n";
        }
        return result;
    }
    return loadToReg(val, destReg);
}

void TargetCodeGen::emitRet(IR::Instruction& inst) {
    if (inst.getNumOperands() > 0) {
        auto* retTy = inst.getOperand(0)->getType();
        if (retTy && retTy->isFloat()) {
            std::string code = emitValueToReg(inst.getOperand(0), "fa0");
            emitter.emitText(code);
        } else {
            std::string code = emitValueToReg(inst.getOperand(0), "a0");
            emitter.emitText(code);
        }
    }
    emitter.emitText("  j       " + currentFunc->getName() + "_exit");
}

void TargetCodeGen::emitBr(IR::Instruction& inst) {
    auto* target = dynamic_cast<IR::BasicBlock*>(inst.getOperand(0));
    emitter.emitText("  j       .L" + currentFunc->getName() + "_" + target->getName());
}

void TargetCodeGen::emitCondBr(IR::Instruction& inst) {
    std::string code;
    // Use loadToReg to handle constants (li) and variables (mv/stack load)
    code += loadToReg(inst.getOperand(0), "t0");
    auto* thenBB = dynamic_cast<IR::BasicBlock*>(inst.getOperand(1));
    auto* elseBB = dynamic_cast<IR::BasicBlock*>(inst.getOperand(2));
    code += "  bnez    t0, .L" + currentFunc->getName() + "_" + thenBB->getName() + "\n";
    code += "  j       .L" + currentFunc->getName() + "_" + elseBB->getName() + "\n";
    emitter.emitText(code);
}

std::string TargetCodeGen::getValueReg(IR::Value* val) {
    if (regAlloc.hasReg(val)) {
        return regAlloc.getReg(val);
    }
    return "";
}

void TargetCodeGen::emitBinOp(IR::Instruction& inst) {
    std::string code;

    std::string r0 = getValueReg(inst.getOperand(0));
    std::string r1 = getValueReg(inst.getOperand(1));
    std::string rd = getValueReg(&inst);

    bool op0InReg = !r0.empty();
    bool op1InReg = !r1.empty();
    bool rdInReg = !rd.empty();

    if (!op0InReg) code += loadToReg(inst.getOperand(0), "t0");
    if (!op1InReg) code += loadToReg(inst.getOperand(1), "t1");

    std::string op0 = op0InReg ? r0 : "t0";
    std::string op1 = op1InReg ? r1 : "t1";
    std::string dest = rdInReg ? rd : "t0";

    using Opc = IR::Instruction::Opcode;
    switch (inst.getOpcode()) {
    case Opc::ADD:  code += "  addw    " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::SUB:  code += "  subw    " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::MUL:  code += "  mulw    " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::SDIV: code += "  divw    " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::SREM: code += "  remw    " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::AND:  code += "  and     " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::OR:   code += "  or      " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::XOR:  code += "  xor     " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::SHL:  code += "  sllw    " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::ASHR: code += "  sraw    " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::SMULH:
        // smulh: 返回两个 i32 有符号乘积的高 32 位
        // RISC-V: mul 得到 64 位全乘积，srai 32 取高 32 位并符号扩展
        // 使用 t2 作为临时寄存器（避免与 t0/t1 的 operand 加载冲突）
        code += "  mul     t2, " + op0 + ", " + op1 + "\n";
        code += "  srai    " + dest + ", t2, 32\n";
        break;
    default: break;
    }

    if (!rdInReg) {
        code += storeFromReg(&inst, dest);
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitFBinOp(IR::Instruction& inst) {
    std::string code;

    std::string r0 = getValueReg(inst.getOperand(0));
    std::string r1 = getValueReg(inst.getOperand(1));
    std::string rd = getValueReg(&inst);

    bool op0InReg = !r0.empty();
    bool op1InReg = !r1.empty();
    bool rdInReg = !rd.empty();

    if (!op0InReg) code += loadToReg(inst.getOperand(0), "ft0");
    if (!op1InReg) code += loadToReg(inst.getOperand(1), "ft1");

    std::string op0 = op0InReg ? r0 : "ft0";
    std::string op1 = op1InReg ? r1 : "ft1";
    std::string dest = rdInReg ? rd : "ft0";

    using Opc = IR::Instruction::Opcode;
    switch (inst.getOpcode()) {
    case Opc::FADD: code += "  fadd.s  " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::FSUB: code += "  fsub.s  " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::FMUL: code += "  fmul.s  " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    case Opc::FDIV: code += "  fdiv.s  " + dest + ", " + op0 + ", " + op1 + "\n"; break;
    default: break;
    }

    if (!rdInReg) {
        code += storeFromReg(&inst, dest);
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitIcmp(IR::Instruction& inst) {
    auto* condType = inst.getOperand(0)->getType();
    bool isFloat = condType && condType->isFloat();

    if (isFloat) {
        std::string code;

        std::string r0 = getValueReg(inst.getOperand(0));
        std::string r1 = getValueReg(inst.getOperand(1));
        std::string rd = getValueReg(&inst);

        bool op0InReg = !r0.empty();
        bool op1InReg = !r1.empty();
        bool rdInReg = !rd.empty();

        if (!op0InReg) code += loadToReg(inst.getOperand(0), "ft0");
        if (!op1InReg) code += loadToReg(inst.getOperand(1), "ft1");

        std::string op0 = op0InReg ? r0 : "ft0";
        std::string op1 = op1InReg ? r1 : "ft1";
        std::string dest = rdInReg ? rd : "t0";

        std::string cond = inst.getName();
        if (cond == "eq")  code += "  feq.s   " + dest + ", " + op0 + ", " + op1 + "\n";
        else if (cond == "ne") code += "  feq.s   " + dest + ", " + op0 + ", " + op1 + "\n  xori    " + dest + ", " + dest + ", 1\n";
        else if (cond == "slt") code += "  flt.s   " + dest + ", " + op0 + ", " + op1 + "\n";
        else if (cond == "sle") code += "  fle.s   " + dest + ", " + op0 + ", " + op1 + "\n";
        else if (cond == "sgt") code += "  flt.s   " + dest + ", " + op1 + ", " + op0 + "\n";
        else if (cond == "sge") code += "  fle.s   " + dest + ", " + op1 + ", " + op0 + "\n";
        else code += "  flt.s   " + dest + ", " + op0 + ", " + op1 + "\n";

        if (!rdInReg) {
            code += storeFromReg(&inst, dest);
        }
        emitter.emitText(code);
        return;
    }

    std::string code;

    std::string r0 = getValueReg(inst.getOperand(0));
    std::string r1 = getValueReg(inst.getOperand(1));
    std::string rd = getValueReg(&inst);

    bool op0InReg = !r0.empty();
    bool op1InReg = !r1.empty();
    bool rdInReg = !rd.empty();

    if (!op0InReg) code += loadToReg(inst.getOperand(0), "t0");
    if (!op1InReg) code += loadToReg(inst.getOperand(1), "t1");

    std::string op0 = op0InReg ? r0 : "t0";
    std::string op1 = op1InReg ? r1 : "t1";
    std::string dest = rdInReg ? rd : "t0";

    std::string cond = inst.getName();
    if (cond == "eq")  code += "  sub     " + dest + ", " + op0 + ", " + op1 + "\n  seqz    " + dest + ", " + dest + "\n";
    else if (cond == "ne")  code += "  sub     " + dest + ", " + op0 + ", " + op1 + "\n  snez    " + dest + ", " + dest + "\n";
    else if (cond == "slt") code += "  slt     " + dest + ", " + op0 + ", " + op1 + "\n";
    else if (cond == "sle") code += "  slt     " + dest + ", " + op1 + ", " + op0 + "\n  xori    " + dest + ", " + dest + ", 1\n";
    else if (cond == "sgt") code += "  slt     " + dest + ", " + op1 + ", " + op0 + "\n";
    else if (cond == "sge") code += "  slt     " + dest + ", " + op0 + ", " + op1 + "\n  xori    " + dest + ", " + dest + ", 1\n";
    else code += "  slt     " + dest + ", " + op0 + ", " + op1 + "\n";

    if (!rdInReg) {
        code += storeFromReg(&inst, dest);
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitFcmp(IR::Instruction& inst) {
    std::string code;

    std::string r0 = getValueReg(inst.getOperand(0));
    std::string r1 = getValueReg(inst.getOperand(1));
    std::string rd = getValueReg(&inst);

    bool op0InReg = !r0.empty();
    bool op1InReg = !r1.empty();
    bool rdInReg = !rd.empty();

    if (!op0InReg) code += loadToReg(inst.getOperand(0), "ft0");
    if (!op1InReg) code += loadToReg(inst.getOperand(1), "ft1");

    std::string op0 = op0InReg ? r0 : "ft0";
    std::string op1 = op1InReg ? r1 : "ft1";
    std::string dest = rdInReg ? rd : "t0";

    std::string cond = inst.getName();
    if (cond == "eq")  code += "  feq.s   " + dest + ", " + op0 + ", " + op1 + "\n";
    else if (cond == "ne") code += "  feq.s   " + dest + ", " + op0 + ", " + op1 + "\n  xori    " + dest + ", " + dest + ", 1\n";
    else if (cond == "slt") code += "  flt.s   " + dest + ", " + op0 + ", " + op1 + "\n";
    else if (cond == "sle") code += "  fle.s   " + dest + ", " + op0 + ", " + op1 + "\n";
    else if (cond == "sgt") code += "  flt.s   " + dest + ", " + op1 + ", " + op0 + "\n";
    else if (cond == "sge") code += "  fle.s   " + dest + ", " + op1 + ", " + op0 + "\n";
    else code += "  flt.s   " + dest + ", " + op0 + ", " + op1 + "\n";

    if (!rdInReg) {
        code += storeFromReg(&inst, dest);
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitLoad(IR::Instruction& inst) {
    std::string code;

    // 检查是否从已提升的 ALLOCA 加载
    auto* ptrOp = inst.getOperand(0);
    auto promotedIt = promotedAllocas.find(ptrOp);
    if (promotedIt != promotedAllocas.end()) {
        // 直接从寄存器获取值，无需内存访问
        std::string allocaReg = promotedIt->second;
        std::string rd = getValueReg(&inst);
        bool rdInReg = !rd.empty();
        bool isFloat = allocaReg[0] == 'f';
        std::string dest = rdInReg ? rd : (isFloat ? "ft0" : "t0");
        if (isFloat) {
            code += "  fmv.s   " + dest + ", " + allocaReg + "\n";
        } else {
            code += "  mv      " + dest + ", " + allocaReg + "\n";
        }
        if (!rdInReg) code += storeFromReg(&inst, dest);
        emitter.emitText(code);
        return;
    }

    // FoldMemoryAccess: 如果指针操作数是一个已被融合的 GEP，
    // 则内联地址计算，避免存储/加载 GEP 结果的往返开销
    auto* gepInst = dynamic_cast<IR::Instruction*>(ptrOp);
    bool isFoldedGep = gepInst && foldedGeps.count(gepInst);
    if (isFoldedGep) {
        code += emitGEPAddressToReg(*gepInst, "t0");
    } else {
        code += loadToReg(ptrOp, "t0");
    }

    auto* loadTy = inst.getType();

    std::string rd = getValueReg(&inst);
    bool rdInReg = !rd.empty();

    if (loadTy && loadTy->isFloat()) {
        std::string dest = rdInReg ? rd : "ft0";
        code += "  flw     " + dest + ", 0(t0)\n";
        if (!rdInReg) code += storeFromReg(&inst, dest);
    } else if (loadTy && loadTy->isPointer()) {
        std::string dest = rdInReg ? rd : "t0";
        code += "  ld      " + dest + ", 0(t0)\n";
        if (!rdInReg) code += storeFromReg(&inst, dest);
    } else {
        std::string dest = rdInReg ? rd : "t0";
        code += "  lw      " + dest + ", 0(t0)\n";
        if (!rdInReg) code += storeFromReg(&inst, dest);
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitStore(IR::Instruction& inst) {
    std::string code;

    // 检查是否存储到已提升的 ALLOCA
    auto* ptrOp = inst.getOperand(1);
    auto promotedIt = promotedAllocas.find(ptrOp);
    if (promotedIt != promotedAllocas.end()) {
        // 直接存储到寄存器，无需内存访问
        std::string allocaReg = promotedIt->second;
        auto* valTy = inst.getOperand(0)->getType();
        bool isFloat = valTy && valTy->isFloat();
        std::string srcReg = getValueReg(inst.getOperand(0));
        if (!srcReg.empty()) {
            // 源操作数已经在寄存器中，直接 mv
            if (isFloat) {
                code += "  fmv.s   " + allocaReg + ", " + srcReg + "\n";
            } else {
                code += "  mv      " + allocaReg + ", " + srcReg + "\n";
            }
        } else {
            // 源操作数不在寄存器中，需要先加载
            if (isFloat) {
                code += loadToReg(inst.getOperand(0), "ft0");
                code += "  fmv.s   " + allocaReg + ", ft0\n";
            } else {
                code += loadToReg(inst.getOperand(0), "t0");
                code += "  mv      " + allocaReg + ", t0\n";
            }
        }
        emitter.emitText(code);
        return;
    }

    // FoldMemoryAccess: 检查指针操作数是否是已融合的 GEP
    auto* gepInst = dynamic_cast<IR::Instruction*>(ptrOp);
    bool isFoldedGep = gepInst && foldedGeps.count(gepInst);

    auto* valTy = inst.getOperand(0)->getType();
    if (valTy && valTy->isFloat()) {
        code += loadToReg(inst.getOperand(0), "ft0");
        // 浮点存储：值在 ft0，地址可用 t0（不与 GEP 内部使用的 t1/t3 冲突）
        if (isFoldedGep) {
            code += emitGEPAddressToReg(*gepInst, "t0");
        } else {
            code += loadToReg(inst.getOperand(1), "t0");
        }
        code += "  fsw     ft0, 0(t0)\n";
    } else if (valTy && valTy->isPointer()) {
        code += loadToReg(inst.getOperand(0), "t0");
        // 指针存储：值在 t0，地址用 t2（避免与 GEP 的 t1 索引寄存器冲突）
        if (isFoldedGep) {
            code += emitGEPAddressToReg(*gepInst, "t2");
        } else {
            code += loadToReg(inst.getOperand(1), "t1");
        }
        code += "  sd      t0, 0(" + std::string(isFoldedGep ? "t2" : "t1") + ")\n";
    } else {
        code += loadToReg(inst.getOperand(0), "t0");
        // 整数存储：值在 t0，地址用 t2（避免与 GEP 的 t1 索引寄存器冲突）
        if (isFoldedGep) {
            code += emitGEPAddressToReg(*gepInst, "t2");
        } else {
            code += loadToReg(inst.getOperand(1), "t1");
        }
        code += "  sw      t0, 0(" + std::string(isFoldedGep ? "t2" : "t1") + ")\n";
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitCall(IR::Instruction& inst) {
    std::string code;

    // Save caller-saved registers that are in use before the call
    std::string saveCode, restoreCode;
    int csrOffset = stackSize - 8;
    for (auto& reg : regAlloc.getUsedCalleeSaved()) {
        csrOffset -= 8;
        bool isCallerSaved = false;
        if (reg.size() >= 2 && reg[0] == 't') isCallerSaved = true;           // t3-t6
        if (reg.size() >= 3 && reg[0] == 'f' && reg[1] == 't') isCallerSaved = true; // ft*
        if (isCallerSaved) {
            if (reg[0] == 'f') {
                if (fitsImm12(csrOffset)) {
                    saveCode += "  fsd     " + reg + ", " + std::to_string(csrOffset) + "(sp)\n";
                    restoreCode += "  fld     " + reg + ", " + std::to_string(csrOffset) + "(sp)\n";
                } else {
                    saveCode += "  li      t2, " + std::to_string(csrOffset) + "\n";
                    saveCode += "  add     t2, sp, t2\n";
                    saveCode += "  fsd     " + reg + ", 0(t2)\n";
                    restoreCode += "  li      t2, " + std::to_string(csrOffset) + "\n";
                    restoreCode += "  add     t2, sp, t2\n";
                    restoreCode += "  fld     " + reg + ", 0(t2)\n";
                }
            } else {
                if (fitsImm12(csrOffset)) {
                    saveCode += "  sd      " + reg + ", " + std::to_string(csrOffset) + "(sp)\n";
                    restoreCode += "  ld      " + reg + ", " + std::to_string(csrOffset) + "(sp)\n";
                } else {
                    saveCode += "  li      t2, " + std::to_string(csrOffset) + "\n";
                    saveCode += "  add     t2, sp, t2\n";
                    saveCode += "  sd      " + reg + ", 0(t2)\n";
                    restoreCode += "  li      t2, " + std::to_string(csrOffset) + "\n";
                    restoreCode += "  add     t2, sp, t2\n";
                    restoreCode += "  ld      " + reg + ", 0(t2)\n";
                }
            }
        }
    }
    code += saveCode;

    unsigned numArgs = inst.getNumOperands() - 1;
    unsigned iReg = 0;   // Next available integer argument register (a0-a7)
    unsigned fReg = 0;   // Next available float argument register (fa0-fa7)
    unsigned stackIdx = 0; // Stack parameter offset counter

    for (unsigned i = 0; i < numArgs; ++i) {
        auto* argVal = inst.getOperand(i + 1);
        auto* argTy = argVal->getType();
        bool isFloat = argTy && argTy->isFloat();
        bool isPtr = argTy && argTy->isPointer();

        if (isFloat && fReg < 8) {
            std::string reg = std::string("fa") + std::to_string(fReg++);
            code += loadToReg(argVal, reg);
        } else if (!isFloat && iReg < 8) {
            std::string reg = std::string("a") + std::to_string(iReg++);
            code += loadToReg(argVal, reg);
        } else {
            // Arguments beyond registers: pass on the stack
            int stackOffset = stackIdx * 8;
            stackIdx++;
            if (isFloat) {
                code += loadToReg(argVal, "ft0");
                if (fitsImm12(stackOffset)) {
                    code += "  fsw     ft0, " + std::to_string(stackOffset) + "(sp)\n";
                } else {
                    code += "  li      t1, " + std::to_string(stackOffset) + "\n";
                    code += "  add     t1, sp, t1\n";
                    code += "  fsw     ft0, 0(t1)\n";
                }
            } else if (isPtr) {
                code += loadToReg(argVal, "t0");
                if (fitsImm12(stackOffset)) {
                    code += "  sd      t0, " + std::to_string(stackOffset) + "(sp)\n";
                } else {
                    code += "  li      t1, " + std::to_string(stackOffset) + "\n";
                    code += "  add     t1, sp, t1\n";
                    code += "  sd      t0, 0(t1)\n";
                }
            } else {
                code += loadToReg(argVal, "t0");
                if (fitsImm12(stackOffset)) {
                    code += "  sw      t0, " + std::to_string(stackOffset) + "(sp)\n";
                } else {
                    code += "  li      t1, " + std::to_string(stackOffset) + "\n";
                    code += "  add     t1, sp, t1\n";
                    code += "  sw      t0, 0(t1)\n";
                }
            }
        }
    }

    std::string calleeName = inst.getOperand(0)->getName();
    code += "  call    " + calleeName + "\n";

    code += restoreCode;

    auto* retTy = inst.getType();
    if (!retTy->isVoid()) {
        if (retTy->isFloat()) {
            code += storeFromReg(&inst, "fa0");
        } else {
            code += storeFromReg(&inst, "a0");
        }
    }

    emitter.emitText(code);
}

void TargetCodeGen::emitGetElementPtr(IR::Instruction& inst) {
    // 如果此 GEP 已被融合到 LOAD/STORE 中，跳过发射
    if (foldedGeps.count(&inst)) return;

    std::string code;

    code += loadToReg(inst.getOperand(0), "t0");

    unsigned numOps = inst.getNumOperands();
    if (numOps >= 2) {
        auto* ptrTy = dynamic_cast<IR::PointerType*>(inst.getOperand(0)->getType());
        IR::Type* curPointee = ptrTy ? ptrTy->getPointeeType() : nullptr;

        // First index (operand 1) is the pointer offset:
        // advance by sizeof(pointee). Do NOT update curPointee here.
        // 优化：跳过常量 0 索引，避免无用的 li+mul+add 指令
        auto* firstIdx = dynamic_cast<IR::ConstantInt*>(inst.getOperand(1));
        bool firstIsZero = firstIdx && firstIdx->getValue() == 0;
        if (!firstIsZero) {
            int ptrStride = curPointee ? getTypeSize(curPointee) : 4;
            code += loadToReg(inst.getOperand(1), "t1");
            if (ptrStride == 1) {
            } else if (ptrStride == 2) {
                code += "  slli    t1, t1, 1\n";
            } else if (ptrStride == 4) {
                code += "  slli    t1, t1, 2\n";
            } else if (ptrStride == 8) {
                code += "  slli    t1, t1, 3\n";
            } else {
                code += "  li      t2, " + std::to_string(ptrStride) + "\n";
                code += "  mul     t1, t1, t2\n";
            }
            code += "  add     t0, t0, t1\n";
        }

        // Remaining indices (operand 2+) are array indices:
        // advance by sizeof(element of current array type), then descend.
        for (unsigned i = 2; i < numOps; ++i) {
            // 优化：跳过常量 0 索引
            auto* idxConst = dynamic_cast<IR::ConstantInt*>(inst.getOperand(i));
            if (idxConst && idxConst->getValue() == 0) {
                // 仍需更新 curPointee 以正确计算后续索引的 stride
                if (curPointee && curPointee->isArray()) {
                    auto* arrTy = dynamic_cast<IR::ArrayType*>(curPointee);
                    curPointee = arrTy->getElementType();
                }
                continue;
            }

            int stride = 4;
            if (curPointee && curPointee->isArray()) {
                auto* arrTy = dynamic_cast<IR::ArrayType*>(curPointee);
                stride = getTypeSize(arrTy->getElementType());
                curPointee = arrTy->getElementType();
            }
            code += loadToReg(inst.getOperand(i), "t1");
            if (stride == 1) {
            } else if (stride == 2) {
                code += "  slli    t1, t1, 1\n";
            } else if (stride == 4) {
                code += "  slli    t1, t1, 2\n";
            } else if (stride == 8) {
                code += "  slli    t1, t1, 3\n";
            } else {
                code += "  li      t2, " + std::to_string(stride) + "\n";
                code += "  mul     t1, t1, t2\n";
            }
            code += "  add     t0, t0, t1\n";
        }
    }

    code += storeFromReg(&inst, "t0");
    emitter.emitText(code);
}

void TargetCodeGen::emitSitofp(IR::Instruction& inst) {
    std::string code;

    std::string rs = getValueReg(inst.getOperand(0));
    std::string rd = getValueReg(&inst);

    bool opInReg = !rs.empty();
    bool rdInReg = !rd.empty();

    std::string src = opInReg ? rs : "t0";
    std::string dest = rdInReg ? rd : "ft0";

    if (!opInReg) code += loadToReg(inst.getOperand(0), "t0");
    code += "  fcvt.s.w " + dest + ", " + src + "\n";

    if (!rdInReg) {
        code += storeFromReg(&inst, dest);
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitFptosi(IR::Instruction& inst) {
    std::string code;

    std::string rs = getValueReg(inst.getOperand(0));
    std::string rd = getValueReg(&inst);

    bool opInReg = !rs.empty();
    bool rdInReg = !rd.empty();

    std::string src = opInReg ? rs : "ft0";
    std::string dest = rdInReg ? rd : "t0";

    if (!opInReg) code += loadToReg(inst.getOperand(0), "ft0");
    code += "  fcvt.w.s " + dest + ", " + src + ", rtz\n";

    if (!rdInReg) {
        code += storeFromReg(&inst, dest);
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitSelect(IR::Instruction& inst) {
    // SELECT %cond, %trueVal, %falseVal -> %result
    // RISC-V has no native select; use branch pattern
    static int selectLabelCounter = 0;
    int labelId = selectLabelCounter++;
    std::string labelFalse = ".Lselect_false_" + std::to_string(labelId);
    std::string labelEnd   = ".Lselect_end_"   + std::to_string(labelId);

    std::string code;
    std::string condReg = getValueReg(inst.getOperand(0));
    std::string trueReg = getValueReg(inst.getOperand(1));
    std::string falseReg = getValueReg(inst.getOperand(2));
    std::string rd = getValueReg(&inst);

    std::string cond = condReg;
    if (cond.empty()) {
        code += loadToReg(inst.getOperand(0), "t3");
        cond = "t3";
    }
    std::string tv = trueReg;
    if (tv.empty()) {
        code += loadToReg(inst.getOperand(1), "t4");
        tv = "t4";
    }
    std::string fv = falseReg;
    if (fv.empty()) {
        code += loadToReg(inst.getOperand(2), "t5");
        fv = "t5";
    }

    std::string dest = rd.empty() ? "t0" : rd;

    bool isFloat = inst.getType()->isFloat();

    if (isFloat) {
        code += "  beqz    " + cond + ", " + labelFalse + "\n";
        code += "  fmv.s   " + dest + ", " + tv + "\n";
        code += "  j       " + labelEnd + "\n";
        code += labelFalse + ":\n";
        code += "  fmv.s   " + dest + ", " + fv + "\n";
        code += labelEnd + ":\n";
    } else {
        code += "  beqz    " + cond + ", " + labelFalse + "\n";
        code += "  mv      " + dest + ", " + tv + "\n";
        code += "  j       " + labelEnd + "\n";
        code += labelFalse + ":\n";
        code += "  mv      " + dest + ", " + fv + "\n";
        code += labelEnd + ":\n";
    }

    if (rd.empty()) {
        code += storeFromReg(&inst, dest);
    }

    emitter.emitText(code);
}

// ================================================================
// ALLOCA 寄存器提升
// 将仅用于 LOAD/STORE 的标量 ALLOCA 映射到 callee-saved 寄存器，
// 避免每次访问都通过栈内存（sp+offset）
// ================================================================

bool TargetCodeGen::isAllocaPromotable(IR::Instruction* alloca) const {
    if (alloca->getOpcode() != IR::Instruction::Opcode::ALLOCA) return false;

    // 只提升标量类型（int/float），不提升数组或指针
    auto* ptrTy = dynamic_cast<IR::PointerType*>(alloca->getType());
    if (!ptrTy) return false;
    auto* pointee = ptrTy->getPointeeType();
    if (!pointee || (!pointee->isInteger() && !pointee->isFloat())) return false;

    // 检查所有使用：只允许 LOAD 和 STORE
    for (auto& use : alloca->getUses()) {
        auto* userInst = dynamic_cast<IR::Instruction*>(use.user);
        if (!userInst) return false;
        auto op = userInst->getOpcode();
        if (op != IR::Instruction::Opcode::LOAD && op != IR::Instruction::Opcode::STORE) {
            return false; // GEP 或 CALL 参数等，不能提升
        }
    }

    return true;
}

void TargetCodeGen::promoteAllocasInFunction(IR::Function& func) {
    // 可用的 callee-saved 寄存器池（按优先级排序）
    static const std::vector<std::string> intRegPool = {
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11"
    };
    static const std::vector<std::string> floatRegPool = {
        "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
        "fs8", "fs9", "fs10", "fs11"
    };

    int intIdx = 0, floatIdx = 0;

    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::ALLOCA) continue;
            if (!isAllocaPromotable(inst.get())) continue;

            auto* ptrTy = dynamic_cast<IR::PointerType*>(inst->getType());
            auto* pointee = ptrTy->getPointeeType();
            bool isFloat = pointee->isFloat();

            std::string reg;
            if (isFloat) {
                while (floatIdx < static_cast<int>(floatRegPool.size()) &&
                       regAlloc.isRegReserved(floatRegPool[floatIdx])) {
                    floatIdx++;
                }
                if (floatIdx < static_cast<int>(floatRegPool.size())) {
                    reg = floatRegPool[floatIdx++];
                }
            } else {
                while (intIdx < static_cast<int>(intRegPool.size()) &&
                       regAlloc.isRegReserved(intRegPool[intIdx])) {
                    intIdx++;
                }
                if (intIdx < static_cast<int>(intRegPool.size())) {
                    reg = intRegPool[intIdx++];
                }
            }

            if (!reg.empty()) {
                promotedAllocas[inst.get()] = reg;
            }
        }
    }
}

void TargetCodeGen::collectGlobalAddresses(IR::Function& func) {
    // 收集函数中引用的全局变量
    // 只缓存数组全局变量的地址（标量全局变量会被 GlobalVariablePromotion
    // 提升为 ALLOCA，无需缓存地址）
    std::set<IR::GlobalVariable*> usedGlobals;
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                if (auto* gv = dynamic_cast<IR::GlobalVariable*>(inst->getOperand(i))) {
                    // 只缓存数组类型的全局变量（标量会被提升为 ALLOCA）
                    auto* ptrTy = dynamic_cast<IR::PointerType*>(gv->getType());
                    if (ptrTy) {
                        auto* pointee = ptrTy->getPointeeType();
                        if (pointee && pointee->isArray()) {
                            usedGlobals.insert(gv);
                        }
                    }
                }
            }
        }
    }

    // 收集已使用的寄存器（promotedAllocas 已分配）
    std::set<std::string> usedRegs;
    for (auto& [alloca, reg] : promotedAllocas) {
        usedRegs.insert(reg);
    }

    // 为每个全局变量分配 callee-saved 寄存器（跳过已使用的）
    static const std::vector<std::string> intRegPool = {
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11"
    };

    int regIdx = 0;
    for (auto* gv : usedGlobals) {
        while (regIdx < static_cast<int>(intRegPool.size()) &&
               usedRegs.count(intRegPool[regIdx])) {
            regIdx++;
        }
        if (regIdx < static_cast<int>(intRegPool.size())) {
            std::string reg = intRegPool[regIdx++];
            globalAddrCache[gv] = reg;
        }
    }
}

} // namespace Backend