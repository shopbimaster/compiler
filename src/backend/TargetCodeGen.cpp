#include "backend/TargetCodeGen.h"
#include <algorithm>
#include <cassert>

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
            emitGlobalInitData(gv->getInitializer(), pointee, "  ");
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
            if (auto* ci = dynamic_cast<IR::ConstantInt*>(gv->getInitializer())) {
                emitter.emitData("  .word   " + std::to_string(ci->getValue()));
            } else if (auto* cf = dynamic_cast<IR::ConstantFloat*>(gv->getInitializer())) {
                union { float f; uint32_t i; } u;
                u.f = static_cast<float>(cf->getValue());
                emitter.emitData("  .word   " + std::to_string(u.i));
            } else {
                emitter.emitData("  .word   0");
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
    computeStackLayout(func);

    regAlloc.allocate(func);

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
    stackSize = 0;

    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::ALLOCA) {
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

void TargetCodeGen::emitPrologue(IR::Function& func) {
    emitter.emitText("  addi    sp, sp, -" + std::to_string(stackSize));

    if (stackSize > 0) {
        emitter.emitText("  sd      ra, " + std::to_string(stackSize - 8) + "(sp)");
    }

    int csrOffset = stackSize - 8;
    for (auto& reg : regAlloc.getUsedCalleeSaved()) {
        csrOffset -= 8;
        if (reg[0] == 'f') {
            emitter.emitText("  fsd     " + reg + ", " + std::to_string(csrOffset) + "(sp)");
        } else {
            emitter.emitText("  sd      " + reg + ", " + std::to_string(csrOffset) + "(sp)");
        }
    }

    auto* ft = func.getFunctionType();
    for (unsigned i = 0; i < func.getNumArgs(); ++i) {
        auto* arg = func.getArg(i);
        int offset = getStackOffset(arg);
        std::string argReg = (i < 8)
            ? std::string("a") + std::to_string(i)
            : "t0";
        if (ft->getParamTypes()[i]->isInteger()) {
            emitter.emitText("  sw      " + argReg + ", " + std::to_string(offset) + "(sp)");
        } else if (ft->getParamTypes()[i]->isFloat()) {
            emitter.emitText("  fsw     " + std::string("fa") + std::to_string(i) + ", " + std::to_string(offset) + "(sp)");
        } else {
            emitter.emitText("  sd      " + argReg + ", " + std::to_string(offset) + "(sp)");
        }
    }

    for (unsigned i = 0; i < func.getNumArgs(); ++i) {
        auto* arg = func.getArg(i);
        if (regAlloc.hasReg(arg)) {
            int offset = getStackOffset(arg);
            std::string r = regAlloc.getReg(arg);
            auto* pt = ft->getParamTypes()[i];
            if (pt->isFloat()) {
                emitter.emitText("  flw     " + r + ", " + std::to_string(offset) + "(sp)");
            } else if (pt->isPointer()) {
                emitter.emitText("  ld      " + r + ", " + std::to_string(offset) + "(sp)");
            } else {
                emitter.emitText("  lw      " + r + ", " + std::to_string(offset) + "(sp)");
            }
        }
    }
}

void TargetCodeGen::emitEpilogue(IR::Function& func) {
    if (stackSize > 0) {
        int csrOffset = stackSize - 8;
        for (auto& reg : regAlloc.getUsedCalleeSaved()) {
            csrOffset -= 8;
            if (reg[0] == 'f') {
                emitter.emitText("  fld     " + reg + ", " + std::to_string(csrOffset) + "(sp)");
            } else {
                emitter.emitText("  ld      " + reg + ", " + std::to_string(csrOffset) + "(sp)");
            }
        }
        emitter.emitText("  ld      ra, " + std::to_string(stackSize - 8) + "(sp)");
        emitter.emitText("  addi    sp, sp, " + std::to_string(stackSize));
    }
    emitter.emitText("  ret");
}

void TargetCodeGen::emitBasicBlock(IR::BasicBlock& bb) {
    emitter.emitText("." + bb.getName() + ":");
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
        result += "  li      t2, " + std::to_string(ci->getValue()) + "\n";
        if (!destIsFloat) {
            result += "  mv      " + destReg + ", t2\n";
        } else {
            result += "  fmv.w.x " + destReg + ", t2\n";
        }
        return result;
    }
    if (auto* cf = dynamic_cast<IR::ConstantFloat*>(val)) {
        union { float f; uint32_t i; } u;
        u.f = static_cast<float>(cf->getValue());
        result += "  li      t2, " + std::to_string(u.i) + "\n";
        if (!destIsFloat) {
            result += "  mv      " + destReg + ", t2\n";
        } else {
            result += "  fmv.w.x " + destReg + ", t2\n";
        }
        return result;
    }

    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(val)) {
        result += "  la      t2, " + gv->getName() + "\n";
        result += "  mv      " + destReg + ", t2\n";
        return result;
    }

    int offset = getStackOffset(val);

    if (allocaOffset.find(val) != allocaOffset.end()) {
        if (offset != 0) {
            result += "  addi    " + destReg + ", sp, " + std::to_string(offset) + "\n";
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
            if (srcFloat && dstFloat) {
                result += "  fmv.s   " + destReg + ", " + r + "\n";
            } else if (srcFloat && !dstFloat) {
                result += "  fmv.x.w " + destReg + ", " + r + "\n";
            } else if (!srcFloat && dstFloat) {
                result += "  fmv.w.x " + destReg + ", " + r + "\n";
            } else {
                result += "  mv      " + destReg + ", " + r + "\n";
            }
        }
        return result;
    }

    auto* ty = val->getType();
    if (ty->isFloat()) {
        result += "  flw     " + destReg + ", " + std::to_string(offset) + "(sp)\n";
    } else if (ty->isPointer()) {
        result += "  ld      " + destReg + ", " + std::to_string(offset) + "(sp)\n";
    } else {
        result += "  lw      " + destReg + ", " + std::to_string(offset) + "(sp)\n";
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
            if (srcFloat && dstFloat) {
                result += "  fmv.s   " + r + ", " + srcReg + "\n";
            } else if (srcFloat && !dstFloat) {
                result += "  fmv.x.w " + r + ", " + srcReg + "\n";
            } else if (!srcFloat && dstFloat) {
                result += "  fmv.w.x " + r + ", " + srcReg + "\n";
            } else {
                result += "  mv      " + r + ", " + srcReg + "\n";
            }
        }
        int offset = getStackOffset(val);
        auto* ty = val->getType();
        if (ty && ty->isFloat()) {
            result += "  fsw     " + r + ", " + std::to_string(offset) + "(sp)\n";
        } else if (ty && ty->isPointer()) {
            result += "  sd      " + r + ", " + std::to_string(offset) + "(sp)\n";
        } else {
            result += "  sw      " + r + ", " + std::to_string(offset) + "(sp)\n";
        }
        return result;
    }

    int offset = getStackOffset(val);
    auto* ty = val->getType();
    if (ty && ty->isFloat()) {
        result += "  fsw     " + srcReg + ", " + std::to_string(offset) + "(sp)\n";
    } else if (ty && ty->isPointer()) {
        result += "  sd      " + srcReg + ", " + std::to_string(offset) + "(sp)\n";
    } else {
        result += "  sw      " + srcReg + ", " + std::to_string(offset) + "(sp)\n";
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
        result += "  li      t2, " + std::to_string(ci->getValue()) + "\n";
        if (destIsFloat) {
            result += "  fmv.w.x " + destReg + ", t2\n";
        } else {
            result += "  mv      " + destReg + ", t2\n";
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
    emitter.emitText("  j       ." + target->getName());
}

void TargetCodeGen::emitCondBr(IR::Instruction& inst) {
    std::string code;
    if (regAlloc.hasReg(inst.getOperand(0))) {
        code += "  mv      t0, " + regAlloc.getReg(inst.getOperand(0)) + "\n";
    } else {
        int offset = getStackOffset(inst.getOperand(0));
        code += "  lw      t0, " + std::to_string(offset) + "(sp)\n";
    }
    auto* thenBB = dynamic_cast<IR::BasicBlock*>(inst.getOperand(1));
    auto* elseBB = dynamic_cast<IR::BasicBlock*>(inst.getOperand(2));
    code += "  bnez    t0, ." + thenBB->getName() + "\n";
    code += "  j       ." + elseBB->getName() + "\n";
    emitter.emitText(code);
}

void TargetCodeGen::emitBinOp(IR::Instruction& inst) {
    std::string code;
    code += loadToReg(inst.getOperand(0), "t0");
    code += loadToReg(inst.getOperand(1), "t1");

    using Opc = IR::Instruction::Opcode;
    switch (inst.getOpcode()) {
    case Opc::ADD:  code += "  add     t0, t0, t1\n"; break;
    case Opc::SUB:  code += "  sub     t0, t0, t1\n"; break;
    case Opc::MUL:  code += "  mul     t0, t0, t1\n"; break;
    case Opc::SDIV: code += "  div     t0, t0, t1\n"; break;
    case Opc::SREM: code += "  rem     t0, t0, t1\n"; break;
    case Opc::AND:  code += "  and     t0, t0, t1\n"; break;
    case Opc::OR:   code += "  or      t0, t0, t1\n"; break;
    case Opc::XOR:  code += "  xor     t0, t0, t1\n"; break;
    case Opc::SHL:  code += "  sll     t0, t0, t1\n"; break;
    case Opc::ASHR: code += "  sra     t0, t0, t1\n"; break;
    default: break;
    }

    code += storeFromReg(&inst, "t0");
    emitter.emitText(code);
}

void TargetCodeGen::emitFBinOp(IR::Instruction& inst) {
    std::string code;
    code += loadToReg(inst.getOperand(0), "ft0");
    code += loadToReg(inst.getOperand(1), "ft1");

    using Opc = IR::Instruction::Opcode;
    switch (inst.getOpcode()) {
    case Opc::FADD: code += "  fadd.s  ft0, ft0, ft1\n"; break;
    case Opc::FSUB: code += "  fsub.s  ft0, ft0, ft1\n"; break;
    case Opc::FMUL: code += "  fmul.s  ft0, ft0, ft1\n"; break;
    case Opc::FDIV: code += "  fdiv.s  ft0, ft0, ft1\n"; break;
    default: break;
    }

    code += storeFromReg(&inst, "ft0");
    emitter.emitText(code);
}

void TargetCodeGen::emitIcmp(IR::Instruction& inst) {
    auto* condType = inst.getOperand(0)->getType();
    bool isFloat = condType && condType->isFloat();

    if (isFloat) {
        std::string code;
        code += loadToReg(inst.getOperand(0), "ft0");
        code += loadToReg(inst.getOperand(1), "ft1");

        std::string cond = inst.getName();
        if (cond == "eq")  code += "  feq.s   t0, ft0, ft1\n";
        else if (cond == "ne") code += "  feq.s   t0, ft0, ft1\n  xori    t0, t0, 1\n";
        else if (cond == "slt") code += "  flt.s   t0, ft0, ft1\n";
        else if (cond == "sle") code += "  fle.s   t0, ft0, ft1\n";
        else if (cond == "sgt") code += "  flt.s   t0, ft1, ft0\n";
        else if (cond == "sge") code += "  fle.s   t0, ft1, ft0\n";
        else code += "  flt.s   t0, ft0, ft1\n";

        code += storeFromReg(&inst, "t0");
        emitter.emitText(code);
        return;
    }

    std::string code;
    code += loadToReg(inst.getOperand(0), "t0");
    code += loadToReg(inst.getOperand(1), "t1");

    std::string cond = inst.getName();
    if (cond == "eq")  code += "  sub     t0, t0, t1\n  seqz    t0, t0\n";
    else if (cond == "ne")  code += "  sub     t0, t0, t1\n  snez    t0, t0\n";
    else if (cond == "slt") code += "  slt     t0, t0, t1\n";
    else if (cond == "sle") code += "  slt     t1, t1, t0\n  xori    t0, t1, 1\n";
    else if (cond == "sgt") code += "  slt     t0, t1, t0\n";
    else if (cond == "sge") code += "  slt     t0, t0, t1\n  xori    t0, t0, 1\n";
    else code += "  slt     t0, t0, t1\n";

    code += storeFromReg(&inst, "t0");
    emitter.emitText(code);
}

void TargetCodeGen::emitFcmp(IR::Instruction& inst) {
    std::string code;
    code += loadToReg(inst.getOperand(0), "ft0");
    code += loadToReg(inst.getOperand(1), "ft1");

    std::string cond = inst.getName();
    if (cond == "eq")  code += "  feq.s   t0, ft0, ft1\n";
    else if (cond == "ne") code += "  feq.s   t0, ft0, ft1\n  xori    t0, t0, 1\n";
    else if (cond == "slt") code += "  flt.s   t0, ft0, ft1\n";
    else if (cond == "sle") code += "  fle.s   t0, ft0, ft1\n";
    else if (cond == "sgt") code += "  flt.s   t0, ft1, ft0\n";
    else if (cond == "sge") code += "  fle.s   t0, ft1, ft0\n";
    else code += "  flt.s   t0, ft0, ft1\n";

    code += storeFromReg(&inst, "t0");
    emitter.emitText(code);
}

void TargetCodeGen::emitLoad(IR::Instruction& inst) {
    std::string code;
    code += loadToReg(inst.getOperand(0), "t0");
    auto* loadTy = inst.getType();
    if (loadTy && loadTy->isFloat()) {
        code += "  flw     ft0, 0(t0)\n";
        code += storeFromReg(&inst, "ft0");
    } else {
        code += "  lw      t0, 0(t0)\n";
        code += storeFromReg(&inst, "t0");
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitStore(IR::Instruction& inst) {
    std::string code;
    auto* valTy = inst.getOperand(0)->getType();
    if (valTy && valTy->isFloat()) {
        code += loadToReg(inst.getOperand(0), "ft0");
        code += loadToReg(inst.getOperand(1), "t0");
        code += "  fsw     ft0, 0(t0)\n";
    } else {
        code += loadToReg(inst.getOperand(0), "t0");
        code += loadToReg(inst.getOperand(1), "t1");
        code += "  sw      t0, 0(t1)\n";
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitCall(IR::Instruction& inst) {
    std::string code;

    unsigned numArgs = inst.getNumOperands() - 1;
    for (unsigned i = 0; i < numArgs; ++i) {
        auto* argVal = inst.getOperand(i + 1);
        auto* argTy = argVal->getType();
        if (argTy && argTy->isFloat() && i < 8) {
            std::string fReg = std::string("fa") + std::to_string(i);
            code += loadToReg(argVal, fReg);
        } else {
            std::string argReg = (i < 8) ? std::string("a") + std::to_string(i) : "t1";
            code += loadToReg(argVal, argReg);
        }
    }

    std::string calleeName = inst.getOperand(0)->getName();
    code += "  call    " + calleeName + "\n";

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
    std::string code;

    code += loadToReg(inst.getOperand(0), "t0");

    unsigned numOps = inst.getNumOperands();
    if (numOps >= 3) {
        auto* ptrTy = dynamic_cast<IR::PointerType*>(inst.getOperand(0)->getType());
        IR::Type* curPointee = ptrTy ? ptrTy->getPointeeType() : nullptr;

        unsigned i = 1;
        if (numOps >= 4) {
            i = 2;
        }

        for (; i < numOps; ++i) {
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
    code += loadToReg(inst.getOperand(0), "t0");
    code += "  fcvt.s.w ft0, t0\n";
    code += storeFromReg(&inst, "ft0");
    emitter.emitText(code);
}

void TargetCodeGen::emitFptosi(IR::Instruction& inst) {
    std::string code;
    code += loadToReg(inst.getOperand(0), "ft0");
    code += "  fcvt.w.s t0, ft0, rtz\n";
    code += storeFromReg(&inst, "t0");
    emitter.emitText(code);
}

} // namespace Backend