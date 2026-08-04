#include "backend/TargetCodeGen.h"
#include "backend/PostRAScheduler.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
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

static bool isFloatReg(const std::string& reg) {
    return !reg.empty() && reg[0] == 'f';
}

// Callee-saved registers: s0-s11, fs0-fs11 (preserved across calls)
static bool isCalleeSavedReg(const std::string& reg) {
    if (reg.size() >= 2 && reg[0] == 's' && std::isdigit(static_cast<unsigned char>(reg[1])))
        return true;
    if (reg.size() >= 3 && reg[0] == 'f' && reg[1] == 's' && std::isdigit(static_cast<unsigned char>(reg[2])))
        return true;
    return false;
}

// Caller-saved registers: t0-t6, a0-a7, ft0-ft11, fa0-fa7 (clobbered by calls)
static bool isCallerSavedReg(const std::string& reg) {
    if (reg.size() >= 2 && reg[0] == 't' && std::isdigit(static_cast<unsigned char>(reg[1])))
        return true;
    if (reg.size() >= 2 && reg[0] == 'a' && std::isdigit(static_cast<unsigned char>(reg[1])))
        return true;
    if (reg.size() >= 3 && reg[0] == 'f' && reg[1] == 't' && std::isdigit(static_cast<unsigned char>(reg[2])))
        return true;
    if (reg.size() >= 3 && reg[0] == 'f' && reg[1] == 'a' && std::isdigit(static_cast<unsigned char>(reg[2])))
        return true;
    return false;
}

// Argument registers: a0-a7, fa0-fa7 (used for parameter passing, subject to shuffling)
static bool isArgReg(const std::string& reg) {
    if (reg.size() >= 2 && reg[0] == 'a' && std::isdigit(static_cast<unsigned char>(reg[1])))
        return true;
    if (reg.size() >= 3 && reg[0] == 'f' && reg[1] == 'a' && std::isdigit(static_cast<unsigned char>(reg[2])))
        return true;
    return false;
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
    // Post-RA 局部指令调度：作用于最终汇编文本（真实物理寄存器），隐藏 load-use
    // 延迟。未识别助记符/sp,ra 相关块整块跳过，保证正确性。SCHED_OFF=1 关闭。
    result += postRASchedule(emitter.getTextSection());
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

void TargetCodeGen::detectLoopHeaders(IR::Function& func) {
    loopHeaders.clear();
    // 构建 BB → 索引映射
    std::unordered_map<IR::BasicBlock*, size_t> bbIndex;
    const auto& blocks = func.getBlocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        bbIndex[blocks[i].get()] = i;
    }
    // 遍历每个 BB 的 terminator，检测回边
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto* term = blocks[i]->getTerminator();
        if (!term) continue;
        using Opc = IR::Instruction::Opcode;
        if (term->getOpcode() == Opc::BR) {
            // BR: 1 个操作数 = 目标 BB
            if (term->getNumOperands() >= 1) {
                auto* target = dynamic_cast<IR::BasicBlock*>(term->getOperand(0));
                if (target) {
                    auto it = bbIndex.find(target);
                    if (it != bbIndex.end() && it->second <= i) {
                        loopHeaders.insert(target);
                    }
                }
            }
        } else if (term->getOpcode() == Opc::COND_BR) {
            // COND_BR: 3 个操作数 = cond, thenBB, elseBB
            for (unsigned j = 1; j <= 2 && j < term->getNumOperands(); ++j) {
                auto* target = dynamic_cast<IR::BasicBlock*>(term->getOperand(j));
                if (target) {
                    auto it = bbIndex.find(target);
                    if (it != bbIndex.end() && it->second <= i) {
                        loopHeaders.insert(target);
                    }
                }
            }
        }
    }
}

void TargetCodeGen::detectColdBlocks(IR::Function& func) {
    // A1 冷热分离：保守起步，仅标记 RET 终止的非 entry 块为冷块。
    // 这类块典型为早返回（if (err) return -1;）或错误路径，执行频率低。
    // 将它们剥离到 .text.unlikely 段，让热路径在 .text 段内连续，改善 I-cache 局部性。
    //
    // 排除小函数（blocks.size() < 3）：小函数整体性强，分离反而增加跨段 j 开销。
    // entry 块（i==0）不冷：它是函数入口，必然热。
    coldBlocks.clear();

    const char* off = std::getenv("COLD_SPLIT_OFF");
    if (off && std::string(off) == "1") return;

    const auto& blocks = func.getBlocks();
    if (blocks.size() < 3) return;

    using Opc = IR::Instruction::Opcode;
    for (size_t i = 1; i < blocks.size(); ++i) {  // 跳过 entry（i==0）
        auto* term = blocks[i]->getTerminator();
        if (!term) continue;
        if (term->getOpcode() == Opc::RET) {
            coldBlocks.insert(blocks[i].get());
        }
    }

    const char* dbg = std::getenv("DBG_COLD");
    if (dbg && std::string(dbg) == "1" && !coldBlocks.empty()) {
        std::cerr << "[DBG_COLD] " << func.getName() << ": ";
        for (auto* bb : coldBlocks) std::cerr << bb->getName() << " ";
        std::cerr << "\n";
    }
}

void TargetCodeGen::emitFunction(IR::Function& func) {
    currentFunc = &func;
    promotedAllocas.clear();
    regAlloc.clearReservedRegs();

    // 检测循环头（用于后续对齐）
    detectLoopHeaders(func);
    // 检测冷块（A1 冷热分离，用于两遍发射）
    detectColdBlocks(func);

    // 先收集全局变量地址并分配 callee-saved 寄存器缓存（优先级高于 ALLOCA 提升）
    globalAddrCache.clear();
    collectGlobalAddresses(func);
    for (auto& [gv, reg] : globalAddrCache) {
        regAlloc.reserveReg(reg);
    }

    // 收集大常量偏移并分配 callee-saved 寄存器缓存（优先级介于全局地址和 ALLOCA 之间）
    collectLargeConstants(func);

    // 再提升 ALLOCA 到寄存器（跳过已被全局地址缓存占用的寄存器）
    if (!std::getenv("DEBUG_DISABLE_ALLOCA_PROMOTION")) {
        promoteAllocasInFunction(func);
    }

    computeStackLayout(func);

    // 预留被提升的寄存器，防止寄存器分配器使用它们
    for (auto& [alloca, reg] : promotedAllocas) {
        regAlloc.reserveReg(reg);
    }

    regAlloc.allocate(func);

    // 构建 PHI move 映射表（基于边）
    buildPhiMoveMap(func);

    // 收集可以融合的 GEP+LOAD/STORE 模式（FoldMemoryAccess）
    foldedGeps.clear();
    collectFoldedGeps(func);

    // 收集可以融合的 ICMP+COND_BR 模式
    inlinedIcmps.clear();
    collectInlinedIcmps(func);

    // ★ K1+K2 修复：重建 usedCalleeSaved，移除两种无用寄存器：
    //   K1: coalescePhis 释放的 incoming 原寄存器
    //   K2: 被折叠的 GEP/ICMP 指令的寄存器（从未被写入）
    //   必须在 collectFoldedGeps/collectInlinedIcmps 之后调用
    {
        std::set<IR::Instruction*> deadInsts;
        deadInsts.insert(foldedGeps.begin(), foldedGeps.end());
        deadInsts.insert(inlinedIcmps.begin(), inlinedIcmps.end());
        regAlloc.pruneUnusedCalleeSaved(deadInsts);
    }

    // 将 promoted ALLOCA 的 LOAD 指令映射到 callee-saved 寄存器，
    // 避免后续 emitLoad 生成冗余的 mv 指令。
    // ★ 安全检查：如果同一个 BB 内 LOAD 之后有 STORE 到同一个 ALLOCA，
    //   则不能复用 ALLOCA 寄存器——STORE 会覆写该寄存器，
    //   导致后续使用 LOAD 旧值的指令得到错误值（64_calculator SEGFAULT 根因）。
    for (auto& bb : func.getBlocks()) {
        for (auto it = bb->getInstructions().begin(); it != bb->getInstructions().end(); ++it) {
            auto& inst = *it;
            if (inst->getOpcode() != IR::Instruction::Opcode::LOAD) continue;
            auto* ptrOp = inst->getOperand(0);
            auto promotedIt = promotedAllocas.find(ptrOp);
            if (promotedIt == promotedAllocas.end()) continue;

            // 检查同一 BB 内 LOAD 之后是否有 STORE 到同一个 ALLOCA
            bool hasSubsequentStore = false;
            for (auto it2 = it + 1; it2 != bb->getInstructions().end(); ++it2) {
                if ((*it2)->getOpcode() == IR::Instruction::Opcode::STORE) {
                    if ((*it2)->getOperand(1) == ptrOp) {
                        hasSubsequentStore = true;
                        break;
                    }
                }
            }

            // 只有在没有后续 STORE 时才安全复用 ALLOCA 寄存器
            if (!hasSubsequentStore) {
                regAlloc.setReg(inst.get(), promotedIt->second);
            }
        }
    }

    int savedRegCount = static_cast<int>(regAlloc.getUsedCalleeSaved().size());
    int savedRegSpace = savedRegCount * 8;
    stackSize += savedRegSpace;
    stackSize = (stackSize + 15) & ~15;

    // 函数入口对齐到 16 字节边界，优化 BOOM 取指
    emitter.emitText("  .p2align 4");
    emitter.emitText("  .globl  " + func.getName());
    emitter.emitText("  .type   " + func.getName() + ", @function");
    emitter.emitText(func.getName() + ":");

    emitPrologue(func);

    // ================================================================
    // A1 冷热分离 + Fall-through 优化：两遍发射
    // ================================================================
    // 第一遍：热块到 .text（函数头 .p2align + .globl 已进入 .text 段）
    // 第二遍：冷块到 .section .text.unlikely（若有冷块）
    // .Lfunc_exit + epilogue 固定在热段末尾：热路径 `j .Lfunc_exit` 短距离；
    // 冷块（早返回/错误路径）的 RET 也 `j .Lfunc_exit`（同函数内 j 寻址范围足够）。
    //
    // Fall-through：nextBB 只在当前段序列内有效。跨段时 nextBB=nullptr，
    // emitBr/emitCondBr 自然发显式 j，正确性由 RISC-V j 同函数内寻址保证。
    // 冷块内 PHI moves 在前驱（热块）末尾发射，写入寄存器不依赖段位置，安全。
    {
        auto& blocks = func.getBlocks();
        std::vector<IR::BasicBlock*> hotBlocks;
        std::vector<IR::BasicBlock*> coldBlkList;
        hotBlocks.reserve(blocks.size());
        for (auto& bb : blocks) {
            if (coldBlocks.count(bb.get())) coldBlkList.push_back(bb.get());
            else hotBlocks.push_back(bb.get());
        }

        // 第一遍：热块
        for (size_t i = 0; i < hotBlocks.size(); ++i) {
            nextBB = (i + 1 < hotBlocks.size()) ? hotBlocks[i + 1] : nullptr;
            // 最后热块后紧接 .Lfunc_exit（无论有无冷块，func_exit 始终在热段末尾）
            nextIsExit = (i + 1 == hotBlocks.size());
            emitBasicBlock(*hotBlocks[i]);
        }
        nextBB = nullptr;
        nextIsExit = false;

        // ★ 使用 .L 前缀：func_exit 标签仅作本地跳转目标，不进入符号表。
        // 若不带 .L（如 main_exit:），FPGA 链接器可能将其解析为全局符号，
        // 与 sylib 运行时的 main_exit 函数冲突，导致程序退出时绕过 sylib
        // 的输出处理（01_multiple_returns / 02_ret_in_block 回归根因）。
        emitter.emitText(".L" + func.getName() + "_exit:");
        emitEpilogue(func);

        // 第二遍：冷块到 .text.unlikely
        if (!coldBlkList.empty()) {
            emitter.emitText("  .section .text.unlikely, \"ax\", @progbits");
            for (size_t i = 0; i < coldBlkList.size(); ++i) {
                nextBB = (i + 1 < coldBlkList.size()) ? coldBlkList[i + 1] : nullptr;
                nextIsExit = false;  // 冷块后不接 func_exit（已在热段末尾）
                emitBasicBlock(*coldBlkList[i]);
            }
            nextBB = nullptr;
            nextIsExit = false;
            // 切回 .text 段，保证后续函数的 .p2align/.globl 落在 .text
            emitter.emitText("  .text");
        }
    }

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

    savesRA = false;
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

// 生成 stride 乘法代码：将 srcReg 乘以 stride，结果留在 srcReg
// 优化：stride 为 2 的幂次时使用 slli（1 cycle），否则使用 mul（3 cycle）
// largeConstReg 用于非 2 的幂次大 stride 的 li 临时寄存器
std::string TargetCodeGen::emitStrideMul(const std::string& srcReg, int stride,
                                         const std::string& largeConstReg) {
    std::string result;
    if (stride == 1) {
        // 无操作
    } else if (stride == 2) {
        result += "  slli    " + srcReg + ", " + srcReg + ", 1\n";
    } else if (stride == 4) {
        result += "  slli    " + srcReg + ", " + srcReg + ", 2\n";
    } else if (stride == 8) {
        result += "  slli    " + srcReg + ", " + srcReg + ", 3\n";
    } else if (stride > 0 && (stride & (stride - 1)) == 0) {
        // 2 的幂次（16, 32, 64, ..., 4096, ...）：使用 slli
        int shift = 0;
        int tmp = stride;
        while (tmp > 1) { tmp >>= 1; shift++; }
        result += "  slli    " + srcReg + ", " + srcReg + ", " + std::to_string(shift) + "\n";
    } else {
        // 非 2 的幂次：优先使用常量缓存，避免每次发射 li
        auto cacheIt = constantCache.find(stride);
        if (cacheIt != constantCache.end()) {
            result += "  mul     " + srcReg + ", " + srcReg + ", " + cacheIt->second + "\n";
        } else {
            result += "  li      " + largeConstReg + ", " + std::to_string(stride) + "\n";
            result += "  mul     " + srcReg + ", " + srcReg + ", " + largeConstReg + "\n";
        }
    }
    return result;
}

void TargetCodeGen::emitPrologue(IR::Function& func) {
    // Adjust stack pointer, handling large stack frames
    emitter.emitText(emitSPAddImm(-stackSize));

    // Save ra only for non-leaf functions (leaf functions don't call anything)
    if (savesRA && stackSize > 0) {
        emitter.emitText(emitStackStore("ra", stackSize - 8, "sd"));
    }

    // Save only callee-saved registers (s*, fs*) in prologue.
    // Caller-saved registers (t*, ft*) are saved at call sites.
    int csrOffset = stackSize - 8;
    for (auto& reg : regAlloc.getUsedCalleeSaved()) {
        csrOffset -= 8;
        if (!isCalleeSavedReg(reg)) continue;  // Skip caller-saved
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
            std::string srcReg = std::string("fa") + std::to_string(fReg++);
            if (regAlloc.hasReg(arg)) {
                // 直接从 fa 寄存器 mv 到分配的寄存器，避免栈中转
                std::string dstReg = regAlloc.getReg(arg);
                if (dstReg != srcReg) {
                    bool dstFloat = isFloatReg(dstReg);
                    if (dstFloat) {
                        emitter.emitText("  fmv.s   " + dstReg + ", " + srcReg + "\n");
                    } else {
                        emitter.emitText("  fmv.x.w " + dstReg + ", " + srcReg + "\n");
                    }
                }
            } else {
                emitter.emitText(emitStackStore(srcReg, offset, "fsw"));
            }
        } else if (!pIsFloat && iReg < 8) {
            std::string srcReg = std::string("a") + std::to_string(iReg++);
            if (regAlloc.hasReg(arg)) {
                // 直接从 a 寄存器 mv 到分配的寄存器，避免栈中转
                std::string dstReg = regAlloc.getReg(arg);
                if (dstReg != srcReg) {
                    bool dstFloat = isFloatReg(dstReg);
                    if (dstFloat) {
                        emitter.emitText("  mv      t2, " + srcReg + "\n");
                        emitter.emitText("  " + std::string(pIsPtr ? "fmv.d.x" : "fmv.w.x") + " " + dstReg + ", t2\n");
                    } else {
                        emitter.emitText("  mv      " + dstReg + ", " + srcReg + "\n");
                    }
                }
            } else {
                emitter.emitText(emitStackStore(srcReg, offset, pIsPtr ? "sd" : "sw"));
            }
        } else {
            // Arguments beyond registers: load from the caller's stack frame
            int callerOffset = stackSize + stackParamIdx * 8;
            stackParamIdx++;
            if (pIsFloat) {
                if (regAlloc.hasReg(arg)) {
                    std::string dstReg = regAlloc.getReg(arg);
                    if (isFloatReg(dstReg)) {
                        emitter.emitText(emitStackLoad(dstReg, callerOffset, "flw"));
                    } else {
                        emitter.emitText(emitStackLoad("ft0", callerOffset, "flw"));
                        emitter.emitText("  fmv.x.w " + dstReg + ", ft0\n");
                    }
                } else {
                    emitter.emitText(emitStackLoad("ft0", callerOffset, "flw"));
                    emitter.emitText(emitStackStore("ft0", offset, "fsw"));
                }
            } else if (pIsPtr) {
                if (regAlloc.hasReg(arg)) {
                    std::string dstReg = regAlloc.getReg(arg);
                    if (isFloatReg(dstReg)) {
                        emitter.emitText(emitStackLoad("t1", callerOffset, "ld"));
                        emitter.emitText("  fmv.d.x " + dstReg + ", t1\n");
                    } else {
                        emitter.emitText(emitStackLoad(dstReg, callerOffset, "ld"));
                    }
                } else {
                    emitter.emitText(emitStackLoad("t1", callerOffset, "ld"));
                    emitter.emitText(emitStackStore("t1", offset, "sd"));
                }
            } else {
                if (regAlloc.hasReg(arg)) {
                    std::string dstReg = regAlloc.getReg(arg);
                    if (isFloatReg(dstReg)) {
                        emitter.emitText(emitStackLoad("t1", callerOffset, "lw"));
                        emitter.emitText("  fmv.w.x " + dstReg + ", t1\n");
                    } else {
                        emitter.emitText(emitStackLoad(dstReg, callerOffset, "lw"));
                    }
                } else {
                    emitter.emitText(emitStackLoad("t1", callerOffset, "lw"));
                    emitter.emitText(emitStackStore("t1", offset, "sw"));
                }
            }
        }
    }

    // 初始化全局变量地址缓存
    for (auto& [gv, reg] : globalAddrCache) {
        emitter.emitText("  la      " + reg + ", " + gv->getName());
    }

    // 初始化大常量缓存（GEP 频繁使用的大偏移量）
    for (auto& [val, reg] : constantCache) {
        emitter.emitText("  li      " + reg + ", " + std::to_string(val));
    }
}

void TargetCodeGen::emitEpilogue(IR::Function& func) {
    if (stackSize > 0) {
        // Restore only callee-saved registers (s*, fs*).
        // Caller-saved registers were restored at call sites.
        int csrOffset = stackSize - 8;
        for (auto& reg : regAlloc.getUsedCalleeSaved()) {
            csrOffset -= 8;
            if (!isCalleeSavedReg(reg)) continue;  // Skip caller-saved
            if (reg[0] == 'f') {
                emitter.emitText(emitStackLoad(reg, csrOffset, "fld"));
            } else {
                emitter.emitText(emitStackLoad(reg, csrOffset, "ld"));
            }
        }
        // Restore ra only for non-leaf functions
        if (savesRA) {
            emitter.emitText(emitStackLoad("ra", stackSize - 8, "ld"));
        }
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

            // ★ 跨基本块融合安全检查：
            // 当 GEP 和它的 LOAD/STORE 使用者位于不同基本块时（典型场景：
            // LICM 把 GEP 外提到循环前置块，LOAD 在循环体内），融合会让
            // 代码生成在 LOAD/STORE 处使用 GEP 的操作数（如索引值）来内联
            // 计算地址。但寄存器分配器的活跃性分析只看到 LOAD 使用 GEP
            // 的结果（一个虚拟寄存器），看不到 LOAD 隐式使用了 GEP 的
            // 操作数。这导致 GEP 操作数（如循环不变量 i-1）的活跃范围被
            // 错误地提前结束，寄存器被复用，下一轮迭代读到错误值（77_substr
            // SEGFAULT 根因）。
            // 修复：仅融合同基本块的 GEP+LOAD/STORE。
            if (user->getParent() != inst->getParent())
                continue;

            foldedGeps.insert(inst.get());
        }
    }
}

// 收集可以融合到 COND_BR 的 ICMP 指令：
// 当 ICMP 结果只被单个 COND_BR 使用时，跳过 ICMP 结果计算，
// 直接在 COND_BR 中生成分支指令 (beq/bne/blt/bge)
void TargetCodeGen::collectInlinedIcmps(IR::Function& func) {
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::COND_BR) continue;
            auto* condVal = inst->getOperand(0);
            auto* icmp = dynamic_cast<IR::Instruction*>(condVal);
            if (!icmp || icmp->getOpcode() != IR::Instruction::Opcode::ICMP) continue;
            if (!icmp->hasOneUse()) continue;
            // 浮点比较不支持分支指令，跳过
            auto* op0Ty = icmp->getOperand(0)->getType();
            if (op0Ty && op0Ty->isFloat()) continue;
            inlinedIcmps.insert(icmp);
        }
    }
}

// 将 GEP 的地址计算内联到指定寄存器中，不存储结果
// 使用 addrReg 作为基址/结果寄存器，t1 作为索引寄存器，t3 作为临时乘法寄存器
std::string TargetCodeGen::emitGEPAddressToReg(IR::Instruction& gep,
                                                const std::string& addrReg) {
    std::string code;

    unsigned numOps = gep.getNumOperands();
    auto* ptrTy = dynamic_cast<IR::PointerType*>(gep.getOperand(0)->getType());
    IR::Type* curPointee = ptrTy ? ptrTy->getPointeeType() : nullptr;

    // 快速路径：如果所有索引都是常量，直接计算总偏移量
    // 避免通过 mv+addi 序列，直接生成 addi addrReg, baseReg, totalOffset
    if (numOps >= 2) {
        bool allConst = true;
        int64_t totalOffset = 0;

        auto* firstIdx = dynamic_cast<IR::ConstantInt*>(gep.getOperand(1));
        if (firstIdx) {
            if (firstIdx->getValue() != 0) {
                int ptrStride = curPointee ? getTypeSize(curPointee) : 4;
                totalOffset += (int64_t)firstIdx->getValue() * ptrStride;
            }
        } else {
            allConst = false;
        }
        IR::Type* curType = curPointee;
        for (unsigned i = 2; i < numOps && allConst; ++i) {
            auto* idxConst = dynamic_cast<IR::ConstantInt*>(gep.getOperand(i));
            if (!idxConst) { allConst = false; break; }
            if (idxConst->getValue() == 0) {
                if (curType && curType->isArray()) {
                    auto* arrTy = dynamic_cast<IR::ArrayType*>(curType);
                    curType = arrTy->getElementType();
                }
                continue;
            }
            int stride = 4;
            if (curType && curType->isArray()) {
                auto* arrTy = dynamic_cast<IR::ArrayType*>(curType);
                stride = getTypeSize(arrTy->getElementType());
                curType = arrTy->getElementType();
            }
            totalOffset += (int64_t)idxConst->getValue() * stride;
        }

        if (allConst) {
            // 如果 base 指针在寄存器中，直接从 base 寄存器计算
            if (regAlloc.hasReg(gep.getOperand(0))) {
                std::string baseReg = regAlloc.getReg(gep.getOperand(0));
                if (totalOffset == 0) {
                    code += "  mv      " + addrReg + ", " + baseReg + "\n";
                } else if (-2048 <= totalOffset && totalOffset <= 2047) {
                    code += "  addi    " + addrReg + ", " + baseReg + ", " +
                            std::to_string(totalOffset) + "\n";
                } else {
                    // 大偏移：优先使用常量缓存，避免每次发射 li
                    auto cacheIt = constantCache.find(totalOffset);
                    if (cacheIt != constantCache.end()) {
                        code += "  add     " + addrReg + ", " + baseReg + ", " + cacheIt->second + "\n";
                    } else {
                        code += "  li      t1, " + std::to_string(totalOffset) + "\n";
                        code += "  add     " + addrReg + ", " + baseReg + ", t1\n";
                    }
                }
                return code;
            }
            // base 不在寄存器中，先 loadToReg 再加偏移
            code += loadToReg(gep.getOperand(0), addrReg);
            if (totalOffset != 0) {
                if (-2048 <= totalOffset && totalOffset <= 2047) {
                    code += "  addi    " + addrReg + ", " + addrReg + ", " +
                            std::to_string(totalOffset) + "\n";
                } else {
                    auto cacheIt = constantCache.find(totalOffset);
                    if (cacheIt != constantCache.end()) {
                        code += "  add     " + addrReg + ", " + addrReg + ", " + cacheIt->second + "\n";
                    } else {
                        code += "  li      t1, " + std::to_string(totalOffset) + "\n";
                        code += "  add     " + addrReg + ", " + addrReg + ", t1\n";
                    }
                }
            }
            return code;
        }
    }

    // 一般路径：有变量索引
    // ★ GEP 寻址优化：当基址已在寄存器中时，跳过 mv addrReg,baseReg，
    //   直接在第一条 add/addi 中使用基址寄存器作为源，消除冗余 mv。
    std::string addrSrc;
    {
        std::string r = getValueRegIfAny(gep.getOperand(0));
        if (!r.empty() && r != addrReg) {
            addrSrc = r;  // 基址在寄存器中，跳过 mv
        } else {
            code += loadToReg(gep.getOperand(0), addrReg);
            addrSrc = addrReg;
        }
    }

    if (numOps >= 2) {
        // 第一个索引（operand 1）：指针偏移，乘以 sizeof(pointee)
        auto* firstIdx = dynamic_cast<IR::ConstantInt*>(gep.getOperand(1));
        bool firstIsZero = firstIdx && firstIdx->getValue() == 0;
        if (!firstIsZero) {
            int ptrStride = curPointee ? getTypeSize(curPointee) : 4;
            if (firstIdx) {
                // 常量索引：直接计算偏移量，避免 li+slli+add 序列
                int64_t offset = (int64_t)firstIdx->getValue() * ptrStride;
                if (-2048 <= offset && offset <= 2047) {
                    code += "  addi    " + addrReg + ", " + addrSrc + ", " + std::to_string(offset) + "\n";
                } else {
                    // 大偏移：优先使用常量缓存
                    auto cacheIt = constantCache.find(offset);
                    if (cacheIt != constantCache.end()) {
                        code += "  add     " + addrReg + ", " + addrSrc + ", " + cacheIt->second + "\n";
                    } else {
                        code += "  li      t1, " + std::to_string(offset) + "\n";
                        code += "  add     " + addrReg + ", " + addrSrc + ", t1\n";
                    }
                }
                addrSrc = addrReg;
            } else {
                code += loadToReg(gep.getOperand(1), "t1");
                code += emitStrideMul("t1", ptrStride, "t3");
                code += "  add     " + addrReg + ", " + addrSrc + ", t1\n";
                addrSrc = addrReg;
            }
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
            if (idxConst) {
                // 常量索引：直接计算偏移量
                int64_t offset = (int64_t)idxConst->getValue() * stride;
                if (-2048 <= offset && offset <= 2047) {
                    code += "  addi    " + addrReg + ", " + addrSrc + ", " + std::to_string(offset) + "\n";
                } else {
                    // 大偏移：优先使用常量缓存
                    auto cacheIt = constantCache.find(offset);
                    if (cacheIt != constantCache.end()) {
                        code += "  add     " + addrReg + ", " + addrSrc + ", " + cacheIt->second + "\n";
                    } else {
                        code += "  li      t1, " + std::to_string(offset) + "\n";
                        code += "  add     " + addrReg + ", " + addrSrc + ", t1\n";
                    }
                }
                addrSrc = addrReg;
            } else {
                code += loadToReg(gep.getOperand(i), "t1");
                code += emitStrideMul("t1", stride, "t3");
                code += "  add     " + addrReg + ", " + addrSrc + ", t1\n";
                addrSrc = addrReg;
            }
        }
    }

    // 如果没有任何偏移（addrSrc 仍是 baseReg），需要 mv addrReg, baseReg
    if (addrSrc != addrReg) {
        code += "  mv      " + addrReg + ", " + addrSrc + "\n";
    }
    return code;
}

void TargetCodeGen::emitBasicBlock(IR::BasicBlock& bb) {
    currentBB = &bb;
    // 循环头对齐：BOOM v2 取指带宽 16B/周期，16B 对齐（.p2align 4）确保循环头
    // 不跨取指块边界。32B 对齐（.p2align 5）在 16B 取指单元上浪费 I-cache 空间。
    // 可用 BOOM_ALIGN32=1 强制 32B 对齐做对照实验（BOOM v3 取指块可能 32B）。
    if (loopHeaders.count(&bb)) {
        const char* a = std::getenv("BOOM_ALIGN32");
        emitter.emitText((a && std::string(a) == "1") ? "  .p2align 5" : "  .p2align 4");
    }
    // Use .L prefix for local labels to avoid symbol conflicts across functions
    emitter.emitText(".L" + currentFunc->getName() + "_" + bb.getName() + ":");

    auto& insts = bb.getInstructions();
    for (auto& inst : insts) {
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
    case Opc::WIDE_SMOD_MUL:
        emitWideSmodMul(inst);
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
    case Opc::TRUNC:
        emitCast(inst);
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
    case Opc::PHI:
        // PHI 指令不生成代码，由 emitPhiMovesForEdge 在前驱块的 terminator 中发射寄存器拷贝
        break;
    default:
        emitter.emitText("  # unknown opcode " + std::to_string(static_cast<int>(inst.getOpcode())));
        break;
    }
}

// ================================================================
// PHI 消除实现
// ================================================================

void TargetCodeGen::buildPhiMoveMap(IR::Function& func) {
    phiMoveMap.clear();
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            // 使用 continue 而非 break：某些优化 Pass（如 InstCombine/CodeSink）
            // 可能在 PHI 之前插入非 PHI 指令（GEP/LOAD 等），导致 PHI 不在块首。
            // 必须扫描所有指令以收集全部 PHI 节点。
            if (inst->getOpcode() != IR::Instruction::Opcode::PHI) continue;
            // PHI 的操作数：(val0, predBB0, val1, predBB1, ...)
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                if (!predBB) continue;
                EdgeKey key{predBB, bb.get()};
                phiMoveMap[key].push_back({inst.get(), inst->getOperand(i)});
            }
        }
    }
}

void TargetCodeGen::emitPhiMovesForEdge(IR::BasicBlock* from, IR::BasicBlock* to) {
    if (!from || !to) return;
    auto it = phiMoveMap.find({from, to});
    if (it == phiMoveMap.end() || it->second.empty()) return;

    auto& moves = it->second;

    // 简单情况：只有 1 个 PHI move，直接发射
    if (moves.size() == 1) {
        auto* phi = moves[0].phi;
        auto* incoming = moves[0].incoming;

        bool isFloat = phi->getType() && phi->getType()->isFloat();
        bool isPointer = phi->getType() && phi->getType()->isPointer();
        std::string phiReg = regAlloc.hasReg(phi) ? regAlloc.getReg(phi) : "";
        std::string srcReg = (incoming && regAlloc.hasReg(incoming)) ? regAlloc.getReg(incoming) : "";

        if (!phiReg.empty() && !srcReg.empty()) {
            // 两者都在寄存器中
            if (phiReg != srcReg) {
                emitter.emitText("  " + std::string(isFloat ? "fmv.s" : "mv") + "  " + phiReg + ", " + srcReg);
            }
        } else if (!phiReg.empty()) {
            // PHI 在寄存器，incoming 是常量或在栈上
            if (auto* ci = dynamic_cast<IR::ConstantInt*>(incoming)) {
                emitter.emitText("  li  " + phiReg + ", " + std::to_string(ci->getValue()));
            } else if (auto* cf = dynamic_cast<IR::ConstantFloat*>(incoming)) {
                union { float f; uint32_t i; } u;
                u.f = static_cast<float>(cf->getValue());
                emitter.emitText("  li  t2, " + std::to_string(u.i));
                emitter.emitText("  fmv.w.x  " + phiReg + ", t2");
            } else {
                // 从栈加载
                int srcOffset = getStackOffset(incoming);
                if (srcOffset >= 0) {
                    std::string loadInsn = isFloat ? "flw" : (isPointer ? "ld" : "lw");
                    emitter.emitText(emitStackLoad(phiReg, srcOffset, loadInsn));
                }
            }
        } else if (!srcReg.empty()) {
            // PHI 在栈上，incoming 在寄存器
            int phiOffset = getStackOffset(phi);
            if (phiOffset >= 0) {
                std::string storeInsn = isFloat ? "fsw" : (isPointer ? "sd" : "sw");
                emitter.emitText(emitStackStore(srcReg, phiOffset, storeInsn));
            }
        } else {
            // 两者都在栈上（或 incoming 是常量），通过临时寄存器中转
            int phiOffset = getStackOffset(phi);
            if (phiOffset < 0) return;

            if (auto* ci = dynamic_cast<IR::ConstantInt*>(incoming)) {
                emitter.emitText("  li  t0, " + std::to_string(ci->getValue()));
                emitter.emitText(emitStackStore("t0", phiOffset, isPointer ? "sd" : "sw"));
            } else if (auto* cf = dynamic_cast<IR::ConstantFloat*>(incoming)) {
                union { float f; uint32_t i; } u;
                u.f = static_cast<float>(cf->getValue());
                emitter.emitText("  li  t2, " + std::to_string(u.i));
                emitter.emitText("  fmv.w.x  ft0, t2");
                emitter.emitText(emitStackStore("ft0", phiOffset, "fsw"));
            } else {
                int srcOffset = getStackOffset(incoming);
                if (srcOffset >= 0) {
                    std::string loadInsn = isFloat ? "flw" : (isPointer ? "ld" : "lw");
                    std::string storeInsn = isFloat ? "fsw" : (isPointer ? "sd" : "sw");
                    std::string tmpReg = isFloat ? "ft0" : "t0";
                    emitter.emitText(emitStackLoad(tmpReg, srcOffset, loadInsn));
                    emitter.emitText(emitStackStore(tmpReg, phiOffset, storeInsn));
                }
            }
        }
        return;
    }

    // 多个 PHI moves：寄存器和栈位置必须一起按并行拷贝处理。
    // 只调度 reg->reg 而提前发射 stack->reg 会覆盖仍被其他 move
    // 读取的旧寄存器值（多条 GEP 递推 PHI 很容易触发该模式）。
    struct MovePair {
        std::string destReg;
        std::string srcReg;
        int destOffset;
        int srcOffset;
        std::string destLoc;
        std::string srcLoc;
        bool isFloat;
        bool isPointer;
        IR::Value* incoming;
        bool sourceFromScratch = false;
    };
    std::vector<MovePair> allMoves;

    auto locationKey = [](const std::string& reg, int offset) {
        if (!reg.empty()) return std::string("reg:") + reg;
        if (offset >= 0) return std::string("stack:") + std::to_string(offset);
        return std::string();
    };

    for (auto& m : moves) {
        auto* phi = m.phi;
        auto* incoming = m.incoming;
        bool isFloat = phi->getType() && phi->getType()->isFloat();
        bool isPointer = phi->getType() && phi->getType()->isPointer();

        std::string phiReg = regAlloc.hasReg(phi) ? regAlloc.getReg(phi) : "";
        std::string srcReg = (incoming && regAlloc.hasReg(incoming)) ? regAlloc.getReg(incoming) : "";
        int destOffset = phiReg.empty() ? getStackOffset(phi) : -1;
        bool isConstant = dynamic_cast<IR::ConstantInt*>(incoming) ||
                          dynamic_cast<IR::ConstantFloat*>(incoming);
        int srcOffset = (srcReg.empty() && !isConstant)
                            ? getStackOffset(incoming) : -1;
        std::string destLoc = locationKey(phiReg, destOffset);
        std::string srcLoc = isConstant ? "" : locationKey(srcReg, srcOffset);

        if (destLoc.empty() || (!isConstant && srcLoc.empty()) ||
            (!srcLoc.empty() && destLoc == srcLoc)) {
            continue;
        }
        allMoves.push_back({phiReg, srcReg, destOffset, srcOffset,
                            destLoc, srcLoc, isFloat, isPointer, incoming});
    }

    if (allMoves.empty()) return;

    auto emitMove = [&](MovePair& m, const std::string& savedScratch,
                        const std::string& transferScratch) {
        std::string sourceReg = m.sourceFromScratch ? savedScratch : m.srcReg;
        std::string loadInsn = m.isFloat ? "flw" : (m.isPointer ? "ld" : "lw");
        std::string storeInsn = m.isFloat ? "fsw" : (m.isPointer ? "sd" : "sw");

        if (!m.destReg.empty()) {
            if (!sourceReg.empty()) {
                emitter.emitText("  " + std::string(m.isFloat ? "fmv.s" : "mv") +
                                 "  " + m.destReg + ", " + sourceReg);
            } else if (auto* ci = dynamic_cast<IR::ConstantInt*>(m.incoming)) {
                emitter.emitText("  li  " + m.destReg + ", " +
                                 std::to_string(ci->getValue()));
            } else if (auto* cf = dynamic_cast<IR::ConstantFloat*>(m.incoming)) {
                union { float f; uint32_t i; } u;
                u.f = static_cast<float>(cf->getValue());
                emitter.emitText("  li  t2, " + std::to_string(u.i));
                emitter.emitText("  fmv.w.x  " + m.destReg + ", t2");
            } else {
                emitter.emitText(emitStackLoad(m.destReg, m.srcOffset, loadInsn));
            }
            return;
        }

        if (!sourceReg.empty()) {
            emitter.emitText(emitStackStore(sourceReg, m.destOffset, storeInsn));
        } else if (auto* ci = dynamic_cast<IR::ConstantInt*>(m.incoming)) {
            emitter.emitText("  li  " + transferScratch + ", " +
                             std::to_string(ci->getValue()));
            emitter.emitText(emitStackStore(transferScratch, m.destOffset, storeInsn));
        } else if (auto* cf = dynamic_cast<IR::ConstantFloat*>(m.incoming)) {
            union { float f; uint32_t i; } u;
            u.f = static_cast<float>(cf->getValue());
            emitter.emitText("  li  t2, " + std::to_string(u.i));
            emitter.emitText("  fmv.w.x  " + transferScratch + ", t2");
            emitter.emitText(emitStackStore(transferScratch, m.destOffset, storeInsn));
        } else {
            emitter.emitText(emitStackLoad(transferScratch, m.srcOffset, loadInsn));
            emitter.emitText(emitStackStore(transferScratch, m.destOffset, storeInsn));
        }
    };

    // 分离 int 和 float moves
    std::vector<MovePair*> intMoves, floatMoves;
    for (auto& m : allMoves) {
        if (m.isFloat) floatMoves.push_back(&m);
        else intMoves.push_back(&m);
    }

    // 并行拷贝解析：一个 move 只有在其目标位置不再被其他未完成
    // move 读取时才能执行。环使用保留 scratch 保存一个旧目标值。
    auto processMoves = [&](std::vector<MovePair*>& moveClass,
                            const std::string& savedScratch,
                            const std::string& transferScratch) {
        if (moveClass.empty()) return;
        std::vector<bool> done(moveClass.size(), false);
        int remaining = static_cast<int>(moveClass.size());

        auto topologicalScan = [&]() {
            bool anyProgress = true;
            while (anyProgress && remaining > 0) {
                anyProgress = false;
                for (int i = 0; i < static_cast<int>(moveClass.size()); ++i) {
                    if (done[i]) continue;
                    bool blocked = false;
                    for (int j = 0; j < static_cast<int>(moveClass.size()); ++j) {
                        if (i != j && !done[j] &&
                            moveClass[j]->srcLoc == moveClass[i]->destLoc) {
                            blocked = true;
                            break;
                        }
                    }
                    if (!blocked) {
                        emitMove(*moveClass[i], savedScratch, transferScratch);
                        done[i] = true;
                        remaining--;
                        anyProgress = true;
                    }
                }
            }
        };

        topologicalScan();
        while (remaining > 0) {
            int start = -1;
            for (int i = 0; i < static_cast<int>(moveClass.size()); ++i) {
                if (!done[i]) { start = i; break; }
            }
            if (start < 0) break;

            auto* startMove = moveClass[start];
            std::string loadInsn = startMove->isFloat ? "flw" :
                                   (startMove->isPointer ? "ld" : "lw");
            if (!startMove->destReg.empty()) {
                emitter.emitText("  " + std::string(startMove->isFloat ? "fmv.s" : "mv") +
                                 "  " + savedScratch + ", " + startMove->destReg);
            } else {
                emitter.emitText(emitStackLoad(savedScratch,
                                                startMove->destOffset, loadInsn));
            }
            std::string savedLoc = startMove->destLoc;
            emitMove(*startMove, savedScratch, transferScratch);
            done[start] = true;
            remaining--;

            for (int j = 0; j < static_cast<int>(moveClass.size()); ++j) {
                if (!done[j] && moveClass[j]->srcLoc == savedLoc) {
                    moveClass[j]->sourceFromScratch = true;
                    moveClass[j]->srcLoc = std::string("scratch:") + savedScratch;
                }
            }
            topologicalScan();
        }
    };

    processMoves(intMoves, "t0", "t1");
    processMoves(floatMoves, "ft0", "ft1");
}

std::string TargetCodeGen::emitValueToStack(IR::Value* val, int stackOffset, bool isFloat) {
    std::string result;
    std::string tmpReg = isFloat ? "ft0" : "t0";
    result += loadToReg(val, tmpReg);
    bool isPointer = val && val->getType() && val->getType()->isPointer();
    std::string storeInsn = isFloat ? "fsw" : (isPointer ? "sd" : "sw");
    result += emitStackStore(tmpReg, stackOffset, storeInsn);
    return result;
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
    // ★ 始终发射 j .Lfunc_exit，由 PeepholeOptimizer 做 fall-through 优化。
    // .L 前缀确保退出标签不进入符号表，避免与 sylib 运行时的 main_exit 函数冲突。
    // main 返回后由 _start 调用 sylib 的 main_exit(retval) 输出返回值。
    emitter.emitText("  j       .L" + currentFunc->getName() + "_exit");
}

void TargetCodeGen::emitBr(IR::Instruction& inst) {
    auto* target = dynamic_cast<IR::BasicBlock*>(inst.getOperand(0));
    emitPhiMovesForEdge(currentBB, target);
    // Fall-through 优化：目标恰好是物理下一个块时省掉 j
    if (target == nextBB) return;
    emitter.emitText("  j       .L" + currentFunc->getName() + "_" + target->getName());
}

void TargetCodeGen::emitCondBr(IR::Instruction& inst) {
    auto* thenBB = dynamic_cast<IR::BasicBlock*>(inst.getOperand(1));
    auto* elseBB = dynamic_cast<IR::BasicBlock*>(inst.getOperand(2));

    // ---- Fall-through 优化（BOOM 分支友好布局）----
    // 若 elseBB 是物理下一个块：条件真跳走(then)，条件假 fall-through(else)
    // 若 thenBB 是物理下一个块：反转条件，条件假跳走(else)，条件真 fall-through(then)
    // 否则：两个分支都显式跳转（原行为）
    const bool elseFall = (elseBB == nextBB);
    const bool thenFall = (thenBB == nextBB);
    IR::BasicBlock* fallBB = elseFall ? elseBB : (thenFall ? thenBB : elseBB);
    IR::BasicBlock* awayBB = elseFall ? thenBB : (thenFall ? elseBB : thenBB);
    bool doFallThrough = (elseFall || thenFall);
    const bool reverseCond = thenFall;

    // ★ 安全检查：away 边有 PHI moves 时不能 fall-through。
    // 原因：away 边的 PHI moves + j awayBB 必须内联在当前块末尾，会物理拦截
    // fall-through 路径（fall-through 会误执行 away 的 phi moves 和跳转）。
    // 此时退化为非 fall-through 模式（显式 j fallBB 跳过 away 代码块）。
    //
    // ★★ Self-loop 特殊处理（LoopRotation 关键收益点）：
    // 当 away 边是自环（awayBB == currentBB，即循环回边）时，PHI moves 可安全
    // 内联到分支指令之前，无需创建 edge-split 块（消除 mv+j 开销）。
    //
    // 安全性论证：
    //   - back-edge 路径（cond true → 跳回 body）：moves 正确更新循环迭代变量供
    //     下一迭代使用。✓
    //   - exit 路径（cond false → fall-through 到 fallBB）：moves 已执行但 PHI
    //     目标寄存器在 exit 路径必须不可被读取。LoopRotation 的 fixExitPhis 已将
    //     exit PHI 引用从 header PHI 改为 back-edge 值（存于不同寄存器），故安全。
    //   - 额外保守检查：若 exit 边的 PHI moves 引用了任一 self-loop PHI 目标
    //     （说明 fixExitPhis 未运行或未覆盖），则不内联（回退到 edge-split）。
    //   - ICMP 条件已在 moves 之前求值（结果存于独立寄存器），不受 moves 影响。✓
    const bool selfLoopAway = (awayBB == currentBB);
    if (doFallThrough) {
        auto awayIt = phiMoveMap.find({currentBB, awayBB});
        if (awayIt != phiMoveMap.end() && !awayIt->second.empty()) {
            if (selfLoopAway) {
                // 保守检查：exit 边 PHI moves 不能引用 self-loop PHI 目标
                bool canInline = true;
                auto exitIt = phiMoveMap.find({currentBB, fallBB});
                if (exitIt != phiMoveMap.end() && !exitIt->second.empty()) {
                    // 收集 self-loop PHI 目标（PhiMove::phi 是目标 PHI 指令）
                    std::vector<IR::Value*> selfPhiDests;
                    selfPhiDests.reserve(awayIt->second.size());
                    for (auto& m : awayIt->second) {
                        selfPhiDests.push_back(m.phi);
                    }
                    // exit 边的 incoming 若是 self-loop PHI 目标，则内联会破坏 exit 值
                    for (auto& m : exitIt->second) {
                        for (auto* d : selfPhiDests) {
                            if (m.incoming == d) {
                                canInline = false;
                                break;
                            }
                        }
                        if (!canInline) break;
                    }
                }
                if (canInline) {
                    // 内联 self-loop PHI moves 到分支前（消除 edge-split 块的 mv+j）
                    emitPhiMovesForEdge(currentBB, awayBB);
                    // 清除已内联的 moves，避免后续 fall/away 处理误发射
                    phiMoveMap.erase({currentBB, awayBB});
                } else {
                    doFallThrough = false;
                }
            } else {
                doFallThrough = false;
            }
        }
    }

    // ★ 分支目标标签：
    // - fall-through 时：条件分支直接跳到 awayBB 自身标签（无需 edgeLabel 中转，
    //   避免 away 代码块内联拦截 fall-through 路径）
    // - 非 fall-through 时：条件分支跳到 edgeLabel，由 edgeLabel 块执行 away phi moves + j
    std::string awayTarget;
    std::string edgeLabel;
    if (doFallThrough) {
        awayTarget = ".L" + currentFunc->getName() + "_" + awayBB->getName();
    } else {
        edgeLabel = ".L" + currentFunc->getName() + "_edge_" +
                    std::to_string(labelCounter++);
        awayTarget = edgeLabel;
    }

    // 检查条件是否来自已内联的 ICMP 指令
    auto* condVal = inst.getOperand(0);
    auto* icmp = dynamic_cast<IR::Instruction*>(condVal);
    if (icmp && inlinedIcmps.count(icmp)) {
        std::string cond = icmp->getName(); // eq/ne/slt/sle/sgt/sge
        auto* op0 = icmp->getOperand(0);
        auto* op1 = icmp->getOperand(1);

        std::string code;
        std::string r0 = getValueReg(op0);
        if (r0.empty()) {
            code += loadToReg(op0, "t0");
            r0 = "t0";
        }

        // 反转条件（thenBB fall-through 时）：eq<->ne, slt<->sge, sle<->sgt
        std::string effCond = cond;
        if (reverseCond) {
            if (cond == "eq") effCond = "ne";
            else if (cond == "ne") effCond = "eq";
            else if (cond == "slt") effCond = "sge";
            else if (cond == "sge") effCond = "slt";
            else if (cond == "sle") effCond = "sgt";
            else if (cond == "sgt") effCond = "sle";
        }

        // 与常量 0 比较时使用 beqz/bnez
        auto* ci1 = dynamic_cast<IR::ConstantInt*>(op1);
        if (ci1 && ci1->getValue() == 0 && (effCond == "eq" || effCond == "ne")) {
            if (effCond == "eq")
                code += "  beqz    " + r0 + ", " + awayTarget + "\n";
            else
                code += "  bnez    " + r0 + ", " + awayTarget + "\n";
        } else {
            // 两个寄存器操作数的分支指令
            std::string r1 = getValueReg(op1);
            if (r1.empty()) {
                code += loadToReg(op1, "t1");
                r1 = "t1";
            }
            if (effCond == "eq")
                code += "  beq     " + r0 + ", " + r1 + ", " + awayTarget + "\n";
            else if (effCond == "ne")
                code += "  bne     " + r0 + ", " + r1 + ", " + awayTarget + "\n";
            else if (effCond == "slt")
                code += "  blt     " + r0 + ", " + r1 + ", " + awayTarget + "\n";
            else if (effCond == "sle")
                code += "  bge     " + r1 + ", " + r0 + ", " + awayTarget + "\n";
            else if (effCond == "sgt")
                code += "  blt     " + r1 + ", " + r0 + ", " + awayTarget + "\n";
            else if (effCond == "sge")
                code += "  bge     " + r0 + ", " + r1 + ", " + awayTarget + "\n";
            else
                code += "  bne     " + r0 + ", " + r1 + ", " + awayTarget + "\n";
        }

        emitter.emitText(code);
    } else {
        // 一般路径：加载条件到 t0，使用 bnez/beqz
        std::string code;
        code += loadToReg(inst.getOperand(0), "t0");
        // 反转时用 beqz（条件假跳走），否则 bnez（条件真跳走）
        code += std::string(reverseCond ? "  beqz    " : "  bnez    ") + "t0, " + awayTarget + "\n";
        emitter.emitText(code);
    }

    // ★ Fall-through 分支：PHI moves + (可选)跳转
    // fall 边的 phi moves 在 fall-through 路径上执行是正确的（为 fallBB 准备 phi 值）
    emitPhiMovesForEdge(currentBB, fallBB);
    if (!doFallThrough) {
        // 非 fall-through：显式跳转到 fallBB（跳过下方的 edgeLabel 块）
        emitter.emitText("  j       .L" + currentFunc->getName() + "_" + fallBB->getName());
        // Away 分支：edgeLabel 标签 + PHI moves + 跳转
        emitter.emitText(edgeLabel + ":");
        emitPhiMovesForEdge(currentBB, awayBB);
        emitter.emitText("  j       .L" + currentFunc->getName() + "_" + awayBB->getName());
    }
    // fall-through 时：fall phi moves 后直接落入 nextBB（fallBB），无 edgeLabel 块
}

std::string TargetCodeGen::getValueReg(IR::Value* val) {
    if (regAlloc.hasReg(val)) {
        return regAlloc.getReg(val);
    }
    return "";
}

std::string TargetCodeGen::getValueRegIfAny(IR::Value* val) {
    // 先检查寄存器分配器
    if (regAlloc.hasReg(val)) {
        return regAlloc.getReg(val);
    }
    // 再检查全局变量地址缓存
    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(val)) {
        auto it = globalAddrCache.find(gv);
        if (it != globalAddrCache.end()) {
            return it->second;
        }
    }
    // 检查已提升的 ALLOCA
    auto promotedIt = promotedAllocas.find(val);
    if (promotedIt != promotedAllocas.end()) {
        return promotedIt->second;
    }
    return "";
}

void TargetCodeGen::emitBinOp(IR::Instruction& inst) {
    std::string code;

    using Opc = IR::Instruction::Opcode;

    // 检查操作数 1 是否是 imm12 范围内的常量，优化 ADD/SUB/AND/OR/XOR/SHL/ASHR/MUL(2的幂)
    auto* ci1 = dynamic_cast<IR::ConstantInt*>(inst.getOperand(1));
    if (ci1 && -2048 <= ci1->getValue() && ci1->getValue() <= 2047) {
        if (inst.getOpcode() == Opc::ADD || inst.getOpcode() == Opc::SUB) {
            std::string r0 = getValueReg(inst.getOperand(0));
            std::string rd = getValueReg(&inst);
            bool op0InReg = !r0.empty();
            bool rdInReg = !rd.empty();

            if (!op0InReg) code += loadToReg(inst.getOperand(0), "t0");
            std::string op0 = op0InReg ? r0 : "t0";
            std::string dest = rdInReg ? rd : "t0";

            int64_t imm = ci1->getValue();
            if (inst.getOpcode() == Opc::SUB) imm = -imm;
            code += "  addiw   " + dest + ", " + op0 + ", " + std::to_string(imm) + "\n";

            if (!rdInReg) code += storeFromReg(&inst, dest);
            emitter.emitText(code);
            return;
        }
        // AND/OR/XOR 与 imm12 常量：用 andi/ori/xori 代替 li+and/or/xor（省 1 条 li）
        // 语义一致：imm12 符号扩展到 64 位，与先 li（符号扩展）再 64 位 and/or/xor 结果相同
        // SHL/ASHR 与 0-31 常量：用 slliw/sraiw 代替 li+sllw/sraw（省 1 条 li）
        // MUL 与 2 的幂（1-2048，log2 0-11）：用 slliw 代替 li+mulw（省 1 条 li + 减少 mul 延迟）
        if (inst.getOpcode() == Opc::AND || inst.getOpcode() == Opc::OR
            || inst.getOpcode() == Opc::XOR || inst.getOpcode() == Opc::SHL
            || inst.getOpcode() == Opc::ASHR || inst.getOpcode() == Opc::MUL) {
            int64_t imm = ci1->getValue();
            std::string mnemonic;
            bool canOptimize = true;
            if (inst.getOpcode() == Opc::AND) mnemonic = "andi";
            else if (inst.getOpcode() == Opc::OR) mnemonic = "ori";
            else if (inst.getOpcode() == Opc::XOR) mnemonic = "xori";
            else if (inst.getOpcode() == Opc::SHL) {
                if (imm < 0 || imm > 31) canOptimize = false;
                else mnemonic = "slliw";
            } else if (inst.getOpcode() == Opc::ASHR) {
                if (imm < 0 || imm > 31) canOptimize = false;
                else mnemonic = "sraiw";
            } else { // MUL
                // 2 的幂检查（imm > 0 且只有 1 个 bit）
                if (imm <= 0 || (imm & (imm - 1)) != 0) canOptimize = false;
                else {
                    int shift = 0;
                    int64_t v = imm;
                    while (v > 1) { v >>= 1; shift++; }
                    if (shift > 31) canOptimize = false;
                    else {
                        mnemonic = "slliw";
                        imm = shift;
                    }
                }
            }
            if (canOptimize) {
                std::string r0 = getValueReg(inst.getOperand(0));
                std::string rd = getValueReg(&inst);
                bool op0InReg = !r0.empty();
                bool rdInReg = !rd.empty();

                if (!op0InReg) code += loadToReg(inst.getOperand(0), "t0");
                std::string op0 = op0InReg ? r0 : "t0";
                std::string dest = rdInReg ? rd : "t0";

                code += "  " + mnemonic + ((int)mnemonic.size() >= 5 ? "   " : "    ")
                        + dest + ", " + op0 + ", " + std::to_string(imm) + "\n";

                if (!rdInReg) code += storeFromReg(&inst, dest);
                emitter.emitText(code);
                return;
            }
        }
    }

    // 检查操作数 0 是否是 imm12 范围内的常量（可交换操作：ADD/AND/OR/XOR/MUL(2的幂)）
    auto* ci0 = dynamic_cast<IR::ConstantInt*>(inst.getOperand(0));
    if (ci0 && -2048 <= ci0->getValue() && ci0->getValue() <= 2047
        && (inst.getOpcode() == Opc::ADD || inst.getOpcode() == Opc::AND
            || inst.getOpcode() == Opc::OR || inst.getOpcode() == Opc::XOR
            || inst.getOpcode() == Opc::MUL)) {
        int64_t imm = ci0->getValue();
        std::string mnemonic;
        bool canOptimize = true;
        if (inst.getOpcode() == Opc::ADD) mnemonic = "addiw";
        else if (inst.getOpcode() == Opc::AND) mnemonic = "andi";
        else if (inst.getOpcode() == Opc::OR) mnemonic = "ori";
        else if (inst.getOpcode() == Opc::XOR) mnemonic = "xori";
        else { // MUL — 仅 2 的幂可优化为 slliw
            if (imm <= 0 || (imm & (imm - 1)) != 0) canOptimize = false;
            else {
                int shift = 0;
                int64_t v = imm;
                while (v > 1) { v >>= 1; shift++; }
                if (shift > 31) canOptimize = false;
                else {
                    mnemonic = "slliw";
                    imm = shift;
                }
            }
        }
        if (canOptimize) {
            std::string r1 = getValueReg(inst.getOperand(1));
            std::string rd = getValueReg(&inst);
            bool op1InReg = !r1.empty();
            bool rdInReg = !rd.empty();

            if (!op1InReg) code += loadToReg(inst.getOperand(1), "t0");
            std::string op1 = op1InReg ? r1 : "t0";
            std::string dest = rdInReg ? rd : "t0";

            code += "  " + mnemonic + ((int)mnemonic.size() >= 5 ? "   " : "    ")
                    + dest + ", " + op1 + ", " + std::to_string(imm) + "\n";

            if (!rdInReg) code += storeFromReg(&inst, dest);
            emitter.emitText(code);
            return;
        }
    }

    // 大常量缓存：当操作数是大常量且已在 constantCache 中时，直接使用缓存的 callee-saved 寄存器
    // 典型场景：mul %i, 5600（数组行步长），避免每次发射 li t2, 5600
    {
        auto tryConstCache = [&](IR::ConstantInt* ci, unsigned opIdx, unsigned otherIdx) -> bool {
            if (!ci) return false;
            int64_t val = ci->getValue();
            if (val >= -2048 && val <= 2047) return false;  // imm12 已由上面处理
            auto cacheIt = constantCache.find(val);
            if (cacheIt == constantCache.end()) return false;

            std::string rOther = getValueReg(inst.getOperand(otherIdx));
            std::string rd = getValueReg(&inst);
            bool otherInReg = !rOther.empty();
            bool rdInReg = !rd.empty();

            if (!otherInReg) code += loadToReg(inst.getOperand(otherIdx), "t0");
            std::string opOther = otherInReg ? rOther : "t0";
            std::string dest = rdInReg ? rd : "t0";
            std::string cachedReg = cacheIt->second;

            switch (inst.getOpcode()) {
            case Opc::ADD:  code += "  addw    " + dest + ", " + opOther + ", " + cachedReg + "\n"; break;
            case Opc::SUB:
                if (opIdx == 1) {
                    // dest = op0 - cachedConst
                    code += "  subw    " + dest + ", " + opOther + ", " + cachedReg + "\n";
                } else {
                    // dest = cachedConst - op1，SUB 不可交换，无法用单条指令完成，回退
                    return false;
                }
                break;
            case Opc::MUL:  code += "  mulw    " + dest + ", " + opOther + ", " + cachedReg + "\n"; break;
            case Opc::AND:  code += "  and     " + dest + ", " + opOther + ", " + cachedReg + "\n"; break;
            case Opc::OR:   code += "  or      " + dest + ", " + opOther + ", " + cachedReg + "\n"; break;
            case Opc::XOR:  code += "  xor     " + dest + ", " + opOther + ", " + cachedReg + "\n"; break;
            default: return false;  // SDIV/SREM/SHL/ASHR 暂不处理
            }

            if (!rdInReg) code += storeFromReg(&inst, dest);
            emitter.emitText(code);
            return true;
        };

        // 先检查操作数 1（非交换操作也适用）
        if (tryConstCache(ci1, 1, 0)) return;
        // 再检查操作数 0（仅交换操作：ADD/MUL/AND/OR/XOR）
        if (ci0) {
            switch (inst.getOpcode()) {
            case Opc::ADD: case Opc::MUL:
            case Opc::AND: case Opc::OR: case Opc::XOR:
                if (tryConstCache(ci0, 0, 1)) return;
                break;
            default: break;
            }
        }
    }

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

void TargetCodeGen::emitWideSmodMul(IR::Instruction& inst) {
    std::string code;

    std::string lhsReg = getValueReg(inst.getOperand(0));
    std::string rhsReg = getValueReg(inst.getOperand(1));
    std::string destReg = getValueReg(&inst);

    if (lhsReg.empty()) {
        code += loadToReg(inst.getOperand(0), "t0");
        lhsReg = "t0";
    }
    if (rhsReg.empty()) {
        code += loadToReg(inst.getOperand(1), "t1");
        rhsReg = "t1";
    }

    // Inputs are sign-extended i32 values. A full RV64 multiply preserves the
    // complete product, unlike mulw, before the signed 64-bit remainder.
    code += "  mul     t2, " + lhsReg + ", " + rhsReg + "\n";
    code += loadToReg(inst.getOperand(2), "t1");

    std::string resultReg = destReg.empty() ? "t0" : destReg;
    code += "  rem     " + resultReg + ", t2, t1\n";
    if (destReg.empty()) {
        code += storeFromReg(&inst, resultReg);
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
    // 如果 ICMP 已被 COND_BR 内联，跳过代码生成
    if (inlinedIcmps.count(&inst)) return;

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

    // 优化：与常量 0 比较时使用 seqz/snez
    auto* ci1 = dynamic_cast<IR::ConstantInt*>(inst.getOperand(1));
    std::string cond = inst.getName();

    if (ci1 && ci1->getValue() == 0 && (cond == "eq" || cond == "ne")) {
        std::string r0 = getValueReg(inst.getOperand(0));
        std::string rd = getValueReg(&inst);
        bool op0InReg = !r0.empty();
        bool rdInReg = !rd.empty();

        if (!op0InReg) code += loadToReg(inst.getOperand(0), "t0");
        std::string op0 = op0InReg ? r0 : "t0";
        std::string dest = rdInReg ? rd : "t0";

        if (cond == "eq") code += "  seqz    " + dest + ", " + op0 + "\n";
        else code += "  snez    " + dest + ", " + op0 + "\n";

        if (!rdInReg) code += storeFromReg(&inst, dest);
        emitter.emitText(code);
        return;
    }

    // 优化：与 imm12 范围内的常量比较时使用 slti（slt/sge/sgt/sle）
    //       或 addi+seqz/snez（eq/ne）代替 li+sub+seqz/snez
    bool usedSlti = false;
    if (ci1 && -2048 <= ci1->getValue() && ci1->getValue() <= 2047
        && (cond == "slt" || cond == "sge" || cond == "sgt" || cond == "sle"
            || cond == "eq" || cond == "ne")) {
        std::string r0 = getValueReg(inst.getOperand(0));
        std::string rd = getValueReg(&inst);
        bool op0InReg = !r0.empty();
        bool rdInReg = !rd.empty();

        if (!op0InReg) code += loadToReg(inst.getOperand(0), "t0");
        std::string op0 = op0InReg ? r0 : "t0";
        std::string dest = rdInReg ? rd : "t0";

        if (cond == "slt") {
            code += "  slti    " + dest + ", " + op0 + ", " + std::to_string(ci1->getValue()) + "\n";
        } else if (cond == "sge") {
            code += "  slti    " + dest + ", " + op0 + ", " + std::to_string(ci1->getValue()) + "\n";
            code += "  xori    " + dest + ", " + dest + ", 1\n";
        } else if (cond == "sgt") {
            code += "  slti    " + dest + ", " + op0 + ", " + std::to_string(ci1->getValue() + 1) + "\n";
            code += "  xori    " + dest + ", " + dest + ", 1\n";
        } else if (cond == "sle") {
            code += "  slti    " + dest + ", " + op0 + ", " + std::to_string(ci1->getValue() + 1) + "\n";
        } else if (cond == "eq") {
            // eq: (op0 - imm == 0) → addi dest, op0, -imm; seqz dest, dest
            code += "  addi    " + dest + ", " + op0 + ", " + std::to_string(-ci1->getValue()) + "\n";
            code += "  seqz    " + dest + ", " + dest + "\n";
        } else { // ne
            // ne: (op0 - imm != 0) → addi dest, op0, -imm; snez dest, dest
            code += "  addi    " + dest + ", " + op0 + ", " + std::to_string(-ci1->getValue()) + "\n";
            code += "  snez    " + dest + ", " + dest + "\n";
        }

        if (!rdInReg) code += storeFromReg(&inst, dest);
        emitter.emitText(code);
        usedSlti = true;
    }

    if (usedSlti) return;

    {
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

    // 地址寄存器优化：如果地址已在寄存器中，直接使用该寄存器，省略 mv t0, addrReg
    std::string addrReg;
    if (isFoldedGep) {
        code += emitGEPAddressToReg(*gepInst, "t0");
        addrReg = "t0";
    } else {
        // 检查地址操作数是否已在寄存器中
        if (regAlloc.hasReg(ptrOp)) {
            addrReg = regAlloc.getReg(ptrOp);
        } else {
            auto allocaIt = promotedAllocas.find(ptrOp);
            if (allocaIt != promotedAllocas.end()) {
                addrReg = allocaIt->second;
            }
        }
        if (addrReg.empty()) {
            code += loadToReg(ptrOp, "t0");
            addrReg = "t0";
        }
    }

    auto* loadTy = inst.getType();

    std::string rd = getValueReg(&inst);
    bool rdInReg = !rd.empty();

    if (loadTy && loadTy->isFloat()) {
        std::string dest = rdInReg ? rd : "ft0";
        code += "  flw     " + dest + ", 0(" + addrReg + ")\n";
        if (!rdInReg) code += storeFromReg(&inst, dest);
    } else if (loadTy && loadTy->isPointer()) {
        std::string dest = rdInReg ? rd : "t0";
        code += "  ld      " + dest + ", 0(" + addrReg + ")\n";
        if (!rdInReg) code += storeFromReg(&inst, dest);
    } else {
        std::string dest = rdInReg ? rd : "t0";
        code += "  lw      " + dest + ", 0(" + addrReg + ")\n";
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
        // 浮点存储优化：值已在寄存器中时直接使用，省略 fmv
        std::string valReg = getValueReg(inst.getOperand(0));
        if (valReg.empty()) {
            code += loadToReg(inst.getOperand(0), "ft0");
            valReg = "ft0";
        }
        // 浮点存储：值在 valReg，地址可用 t0（不与 GEP 内部使用的 t1/t3 冲突）
        if (isFoldedGep) {
            code += emitGEPAddressToReg(*gepInst, "t0");
            code += "  fsw     " + valReg + ", 0(t0)\n";
        } else {
            // 检查地址操作数是否已在寄存器中
            auto* addrOp = inst.getOperand(1);
            std::string addrReg;
            if (regAlloc.hasReg(addrOp)) {
                addrReg = regAlloc.getReg(addrOp);
            } else {
                auto allocaIt = promotedAllocas.find(addrOp);
                if (allocaIt != promotedAllocas.end()) {
                    addrReg = allocaIt->second;
                }
            }
            if (!addrReg.empty()) {
                code += "  fsw     " + valReg + ", 0(" + addrReg + ")\n";
            } else {
                code += loadToReg(addrOp, "t0");
                code += "  fsw     " + valReg + ", 0(t0)\n";
            }
        }
    } else if (valTy && valTy->isPointer()) {
        // 指针存储优化：
        // 1. 如果值是常量 0（null 指针），使用 x0，省略 li 指令
        // 2. 如果值已在寄存器中，直接使用，省略 mv
        auto* valOp = inst.getOperand(0);
        auto* ci = dynamic_cast<IR::ConstantInt*>(valOp);
        bool valIsZero = ci && ci->getValue() == 0;
        std::string valReg;
        if (valIsZero) {
            valReg = "x0";
        } else {
            valReg = getValueReg(valOp);
            if (valReg.empty()) {
                code += loadToReg(valOp, "t0");
                valReg = "t0";
            }
        }
        // 指针存储：值在 valReg，地址用 t2（避免与 GEP 的 t1 索引寄存器冲突）
        if (isFoldedGep) {
            code += emitGEPAddressToReg(*gepInst, "t2");
            code += "  sd      " + valReg + ", 0(t2)\n";
        } else {
            auto* addrOp = inst.getOperand(1);
            std::string addrReg;
            if (regAlloc.hasReg(addrOp)) {
                addrReg = regAlloc.getReg(addrOp);
            } else {
                auto allocaIt = promotedAllocas.find(addrOp);
                if (allocaIt != promotedAllocas.end()) {
                    addrReg = allocaIt->second;
                }
            }
            if (!addrReg.empty()) {
                code += "  sd      " + valReg + ", 0(" + addrReg + ")\n";
            } else {
                code += loadToReg(addrOp, "t1");
                code += "  sd      " + valReg + ", 0(t1)\n";
            }
        }
    } else {
        // 整数存储优化：
        // 1. 如果值是常量 0，使用 x0（硬连线零寄存器），省略 li 指令
        // 2. 如果值已在寄存器中，直接使用，省略 mv 指令
        // 3. 如果地址已在寄存器中，直接使用该寄存器，省略 mv 指令
        auto* valOp = inst.getOperand(0);
        auto* ci = dynamic_cast<IR::ConstantInt*>(valOp);
        bool valIsZero = ci && ci->getValue() == 0;
        std::string valReg;
        if (valIsZero) {
            valReg = "x0";
        } else {
            valReg = getValueReg(valOp);
            if (valReg.empty()) {
                code += loadToReg(valOp, "t0");
                valReg = "t0";
            }
        }
        // 整数存储：值在 valReg，地址用 t2（避免与 GEP 的 t1 索引寄存器冲突）
        if (isFoldedGep) {
            code += emitGEPAddressToReg(*gepInst, "t2");
            code += "  sw      " + valReg + ", 0(t2)\n";
        } else {
            // 检查地址操作数是否已在寄存器中（避免 mv t1, s4; sw t0, 0(t1)）
            auto* addrOp = inst.getOperand(1);
            std::string addrReg;
            if (regAlloc.hasReg(addrOp)) {
                addrReg = regAlloc.getReg(addrOp);
            } else {
                auto allocaIt = promotedAllocas.find(addrOp);
                if (allocaIt != promotedAllocas.end()) {
                    addrReg = allocaIt->second;
                }
            }
            if (!addrReg.empty()) {
                code += "  sw      " + valReg + ", 0(" + addrReg + ")\n";
            } else {
                code += loadToReg(addrOp, "t1");
                code += "  sw      " + valReg + ", 0(t1)\n";
            }
        }
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitCall(IR::Instruction& inst) {
    std::string code;

    // Pre-compute stack offsets for all saved registers (matching prologue layout).
    // This ensures caller-saved save offsets don't overlap with callee-saved saves.
    std::unordered_map<std::string, int> regStackOffset;
    int csrOffset = stackSize - 8;
    for (auto& reg : regAlloc.getUsedCalleeSaved()) {
        csrOffset -= 8;
        regStackOffset[reg] = csrOffset;
    }

    // Save ONLY caller-saved registers that are live across this call.
    // This avoids saving/restoring registers that aren't actually live here.
    auto liveRegs = regAlloc.getRegsLiveAtCall(&inst);
    std::unordered_map<std::string, int> savedRegOffsets;
    std::string saveCode, restoreCode;
    for (auto& reg : liveRegs) {
        if (!isCallerSavedReg(reg)) continue;  // Callee-saved regs are preserved by callee
        auto offsetIt = regStackOffset.find(reg);
        if (offsetIt == regStackOffset.end()) continue;
        int offset = offsetIt->second;
        std::string storeInsn = isFloatReg(reg) ? "fsd" : "sd";
        std::string loadInsn = isFloatReg(reg) ? "fld" : "ld";
        saveCode += emitStackStore(reg, offset, storeInsn);
        restoreCode += emitStackLoad(reg, offset, loadInsn);
        savedRegOffsets[reg] = offset;
    }
    code += saveCode;

    unsigned numArgs = inst.getNumOperands() - 1;

    // Pre-pass: Precisely detect register shuffling conflicts.
    // For each parameter i, if its source register (where the value currently
    // lives) is the target of any earlier parameter j (j < i), the source
    // will be clobbered before parameter i is set up. Save ONLY these
    // conflicting registers to minimize call-site overhead.
    // The main loop's shuffling protection checks savedRegOffsets, which is
    // populated by getRegsLiveAtCall — but parameter values are consumed
    // (not live across) the call, so they aren't included there.
    {
        std::set<std::string> earlierTargetRegs;
        unsigned tiReg = 0, tfReg = 0;
        for (unsigned i = 0; i < numArgs; ++i) {
            auto* argVal = inst.getOperand(i + 1);
            auto* argTy = argVal->getType();
            bool isFloat = argTy && argTy->isFloat();

            // Compute this parameter's target register (same logic as main loop)
            std::string targetReg;
            if (isFloat && tfReg < 8) {
                targetReg = std::string("fa") + std::to_string(tfReg++);
            } else if (!isFloat && tiReg < 8) {
                targetReg = std::string("a") + std::to_string(tiReg++);
            }

            // Check if this parameter's source register conflicts
            if (regAlloc.hasReg(argVal)) {
                std::string srcReg = regAlloc.getReg(argVal);
                if (isArgReg(srcReg) && earlierTargetRegs.count(srcReg)) {
                    // Source register is clobbered by an earlier parameter setup
                    if (savedRegOffsets.find(srcReg) == savedRegOffsets.end()) {
                        auto offsetIt = regStackOffset.find(srcReg);
                        if (offsetIt != regStackOffset.end()) {
                            int offset = offsetIt->second;
                            std::string storeInsn = isFloatReg(srcReg) ? "fsd" : "sd";
                            code += emitStackStore(srcReg, offset, storeInsn);
                            savedRegOffsets[srcReg] = offset;
                        }
                    }
                }
            }

            if (!targetReg.empty()) {
                earlierTargetRegs.insert(targetReg);
            }
        }
    }

    // Set up arguments, handling register shuffling.
    // When a value is in an argument register (a0-a7/fa0-fa7) that was saved
    // to the stack above, load from the saved slot to avoid reading a
    // clobbered register.
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
            // Check if source is in a saved argument register (shuffling risk)
            if (regAlloc.hasReg(argVal)) {
                std::string srcReg = regAlloc.getReg(argVal);
                if (isArgReg(srcReg)) {
                    auto it = savedRegOffsets.find(srcReg);
                    if (it != savedRegOffsets.end()) {
                        code += emitStackLoad(reg, it->second, "fld");
                        continue;
                    }
                }
            }
            code += loadToReg(argVal, reg);
        } else if (!isFloat && iReg < 8) {
            std::string reg = std::string("a") + std::to_string(iReg++);
            if (regAlloc.hasReg(argVal)) {
                std::string srcReg = regAlloc.getReg(argVal);
                if (isArgReg(srcReg)) {
                    auto it = savedRegOffsets.find(srcReg);
                    if (it != savedRegOffsets.end()) {
                        code += emitStackLoad(reg, it->second, "ld");
                        continue;
                    }
                }
            }
            code += loadToReg(argVal, reg);
        } else {
            // Arguments beyond registers: pass on the stack
            int stackOffset = stackIdx * 8;
            stackIdx++;
            if (isFloat) {
                if (regAlloc.hasReg(argVal)) {
                    std::string srcReg = regAlloc.getReg(argVal);
                    if (isArgReg(srcReg)) {
                        auto it = savedRegOffsets.find(srcReg);
                        if (it != savedRegOffsets.end()) {
                            code += emitStackLoad("ft0", it->second, "fld");
                            code += emitStackStore("ft0", stackOffset, "fsw");
                            continue;
                        }
                    }
                }
                code += loadToReg(argVal, "ft0");
                code += emitStackStore("ft0", stackOffset, "fsw");
            } else if (isPtr) {
                if (regAlloc.hasReg(argVal)) {
                    std::string srcReg = regAlloc.getReg(argVal);
                    if (isArgReg(srcReg)) {
                        auto it = savedRegOffsets.find(srcReg);
                        if (it != savedRegOffsets.end()) {
                            code += emitStackLoad("t0", it->second, "ld");
                            code += emitStackStore("t0", stackOffset, "sd");
                            continue;
                        }
                    }
                }
                code += loadToReg(argVal, "t0");
                code += emitStackStore("t0", stackOffset, "sd");
            } else {
                if (regAlloc.hasReg(argVal)) {
                    std::string srcReg = regAlloc.getReg(argVal);
                    if (isArgReg(srcReg)) {
                        auto it = savedRegOffsets.find(srcReg);
                        if (it != savedRegOffsets.end()) {
                            code += emitStackLoad("t0", it->second, "ld");
                            code += emitStackStore("t0", stackOffset, "sw");
                            continue;
                        }
                    }
                }
                code += loadToReg(argVal, "t0");
                code += emitStackStore("t0", stackOffset, "sw");
            }
        }
    }

    std::string calleeName = inst.getOperand(0)->getName();
    code += "  call    " + calleeName + "\n";

    auto* retTy = inst.getType();
    if (retTy->isVoid()) {
        // No return value — just restore caller-saved registers
        code += restoreCode;
    } else {
        bool retIsFloat = retTy->isFloat();
        std::string retReg = retIsFloat ? "fa0" : "a0";
        // Check if the return register will be overwritten by restoreCode
        bool retRegSaved = savedRegOffsets.find(retReg) != savedRegOffsets.end();

        if (retRegSaved) {
            // Return register was saved and will be restored (overwriting the
            // return value). Save the return value to the stack slot FIRST,
            // then restore caller-saved registers, then load to assigned reg.
            int retOffset = getStackOffset(&inst);
            std::string storeInsn = retIsFloat ? "fsw" :
                                    (retTy->isPointer() ? "sd" : "sw");
            code += emitStackStore(retReg, retOffset, storeInsn);

            code += restoreCode;

            // If the CALL result has an assigned register, load from stack.
            // If spilled, the value is already in its stack slot.
            if (regAlloc.hasReg(&inst)) {
                std::string r = regAlloc.getReg(&inst);
                std::string loadInsn = retIsFloat ? "flw" :
                                       (retTy->isPointer() ? "ld" : "lw");
                code += emitStackLoad(r, retOffset, loadInsn);
            }
        } else {
            // Return register was not saved — return value is safe
            code += restoreCode;
            code += storeFromReg(&inst, retReg);
        }
    }

    emitter.emitText(code);
}

void TargetCodeGen::emitGetElementPtr(IR::Instruction& inst) {
    // 如果此 GEP 已被融合到 LOAD/STORE 中，跳过发射
    if (foldedGeps.count(&inst)) return;

    // 快速路径：如果所有索引都是常量，直接计算总偏移量
    // 避免通过临时寄存器 t0 中转（mv t0,base; addi t0,t0,off; mv result,t0 → addi result,base,off）
    {
        auto* ptrTy = dynamic_cast<IR::PointerType*>(inst.getOperand(0)->getType());
        IR::Type* curPointee = ptrTy ? ptrTy->getPointeeType() : nullptr;
        unsigned numOps = inst.getNumOperands();
        bool allConst = true;
        int64_t totalOffset = 0;

        if (numOps >= 2) {
            // 第一个索引
            auto* firstIdx = dynamic_cast<IR::ConstantInt*>(inst.getOperand(1));
            if (firstIdx) {
                if (firstIdx->getValue() != 0) {
                    int ptrStride = curPointee ? getTypeSize(curPointee) : 4;
                    totalOffset += (int64_t)firstIdx->getValue() * ptrStride;
                }
            } else {
                allConst = false;
            }
            // 后续索引
            for (unsigned i = 2; i < numOps && allConst; ++i) {
                auto* idxConst = dynamic_cast<IR::ConstantInt*>(inst.getOperand(i));
                if (!idxConst) { allConst = false; break; }
                if (idxConst->getValue() == 0) {
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
                totalOffset += (int64_t)idxConst->getValue() * stride;
            }
        } else {
            allConst = false;
        }

        if (allConst && totalOffset == 0) {
            // 偏移为 0：直接 mv result, base（或什么都不做）
            // ★ 同时检查 regAlloc、globalAddrCache、promotedAllocas
            if (regAlloc.hasReg(&inst)) {
                std::string dstReg = regAlloc.getReg(&inst);
                std::string srcReg = getValueRegIfAny(inst.getOperand(0));
                if (!srcReg.empty()) {
                    if (dstReg != srcReg) {
                        emitter.emitText("  mv      " + dstReg + ", " + srcReg + "\n");
                    }
                    return;
                }
            }
        } else if (allConst) {
            // 有常量偏移：尽量直接从 base 寄存器计算
            // ★ 同时检查 regAlloc、globalAddrCache、promotedAllocas
            if (regAlloc.hasReg(&inst)) {
                std::string dstReg = regAlloc.getReg(&inst);
                std::string srcReg = getValueRegIfAny(inst.getOperand(0));
                if (!srcReg.empty()) {
                    if (-2048 <= totalOffset && totalOffset <= 2047) {
                        emitter.emitText("  addi    " + dstReg + ", " + srcReg + ", " +
                                         std::to_string(totalOffset) + "\n");
                    } else {
                        auto cacheIt = constantCache.find(totalOffset);
                        if (cacheIt != constantCache.end()) {
                            emitter.emitText("  add     " + dstReg + ", " + srcReg + ", " + cacheIt->second + "\n");
                        } else {
                            emitter.emitText("  li      t1, " + std::to_string(totalOffset) + "\n");
                            emitter.emitText("  add     " + dstReg + ", " + srcReg + ", t1\n");
                        }
                    }
                    return;
                }
            }
        }
    }

    std::string code;

    // ★ GEP 寻址优化：当基址已在寄存器中时，跳过 mv t0,baseReg，
    //   直接在第一条 add/addi 中使用基址寄存器作为源，消除冗余 mv。
    //   addrSrc 追踪当前地址所在寄存器：初始为 baseReg，第一次写入 t0 后改为 t0。
    //   同时检查 regAlloc、globalAddrCache、promotedAllocas（getValueRegIfAny）
    std::string addrSrc;
    {
        std::string r = getValueRegIfAny(inst.getOperand(0));
        if (!r.empty() && r != "t0") {
            addrSrc = r;  // 基址在寄存器中，跳过 mv
        } else {
            code += loadToReg(inst.getOperand(0), "t0");
            addrSrc = "t0";
        }
    }

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
            if (firstIdx) {
                // 常量索引：直接计算偏移量，避免 li+slli+add 序列
                int64_t offset = (int64_t)firstIdx->getValue() * ptrStride;
                if (-2048 <= offset && offset <= 2047) {
                    code += "  addi    t0, " + addrSrc + ", " + std::to_string(offset) + "\n";
                } else {
                    auto cacheIt = constantCache.find(offset);
                    if (cacheIt != constantCache.end()) {
                        code += "  add     t0, " + addrSrc + ", " + cacheIt->second + "\n";
                    } else {
                        code += "  li      t1, " + std::to_string(offset) + "\n";
                        code += "  add     t0, " + addrSrc + ", t1\n";
                    }
                }
                addrSrc = "t0";
            } else {
                code += loadToReg(inst.getOperand(1), "t1");
                code += emitStrideMul("t1", ptrStride, "t2");
                code += "  add     t0, " + addrSrc + ", t1\n";
                addrSrc = "t0";
            }
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
            if (idxConst) {
                // 常量索引：直接计算偏移量
                int64_t offset = (int64_t)idxConst->getValue() * stride;
                if (-2048 <= offset && offset <= 2047) {
                    code += "  addi    t0, " + addrSrc + ", " + std::to_string(offset) + "\n";
                } else {
                    auto cacheIt = constantCache.find(offset);
                    if (cacheIt != constantCache.end()) {
                        code += "  add     t0, " + addrSrc + ", " + cacheIt->second + "\n";
                    } else {
                        code += "  li      t1, " + std::to_string(offset) + "\n";
                        code += "  add     t0, " + addrSrc + ", t1\n";
                    }
                }
                addrSrc = "t0";
            } else {
                code += loadToReg(inst.getOperand(i), "t1");
                code += emitStrideMul("t1", stride, "t2");
                code += "  add     t0, " + addrSrc + ", t1\n";
                addrSrc = "t0";
            }
        }
    }

    // 如果没有任何偏移（addrSrc 仍是 baseReg），需要 mv t0, baseReg
    if (addrSrc != "t0") {
        code += "  mv      t0, " + addrSrc + "\n";
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

void TargetCodeGen::emitCast(IR::Instruction& inst) {
    // ZEXT/SEXT/TRUNC：在 RISC-V 上，i1 比较结果已经是 32 位 0/1，
    // zext/sext i1→i32 等价于 mv；trunc i32→i1 也只需传递低 32 位值
    // （后续 icmp != 0 会正确解释）。直接将源值复制到目标即可。
    std::string code;

    std::string rs = getValueReg(inst.getOperand(0));
    std::string rd = getValueReg(&inst);

    bool opInReg = !rs.empty();
    bool rdInReg = !rd.empty();

    std::string src = opInReg ? rs : "t0";
    std::string dest = rdInReg ? rd : "t0";

    if (!opInReg) code += loadToReg(inst.getOperand(0), "t0");
    if (src != dest) {
        code += "  mv      " + dest + ", " + src + "\n";
    }
    if (!rdInReg) {
        code += storeFromReg(&inst, dest);
    }
    emitter.emitText(code);
}

void TargetCodeGen::emitSelect(IR::Instruction& inst) {
    // SELECT %cond, %trueVal, %falseVal -> %result
    // RISC-V has no native select; use branch pattern or branchless for integer.
    //
    // IMPORTANT: Only t0/t1 may be used as scratch (NOT in INT_REGS pool =
    // s0-s11, t3-t6). t2 is also available but unused here for simplicity.
    //
    // ★ Branchless optimization: SELECT is only produced by IfConversion from
    // COND_BR, so cond is always 0/1 (from ICMP/SEQZ/SNEZ). For integer select
    // with a constant operand, we can use mask arithmetic instead of branches,
    // eliminating 2 branches per select. This is QEMU-safe (only uses t0/t1,
    // does not change register allocation).
    std::string rd = getValueReg(&inst);
    std::string dest = rd.empty() ? "t0" : rd;
    bool isFloat = inst.getType()->isFloat();

    if (!isFloat) {
        auto* trueVal = inst.getOperand(1);
        auto* falseVal = inst.getOperand(2);
        auto* trueConst = dynamic_cast<IR::ConstantInt*>(trueVal);
        auto* falseConst = dynamic_cast<IR::ConstantInt*>(falseVal);

        // Pattern A: select cond(0/1), var, 0
        //   cond=1 → dest=var; cond=0 → dest=0
        //   neg t0, cond  (t0 = -cond = 0 or -1, the bitmask; cond is dead after)
        //   and dest, var, t0
        // Safe: cond is dead after neg, so t0 can be reused. Even if both cond
        // and var need loading, t0=cond→neg t0→t1=var→and dest,t1,t0 works.
        if (falseConst && falseConst->getValue() == 0 && !trueConst) {
            std::string code;
            std::string condReg = getValueReg(inst.getOperand(0));
            std::string trueReg = getValueReg(trueVal);

            std::string cond = condReg;
            if (cond.empty()) { code += loadToReg(inst.getOperand(0), "t0"); cond = "t0"; }
            std::string tv = trueReg;
            if (tv.empty()) { code += loadToReg(trueVal, "t1"); tv = "t1"; }

            code += "  neg     t0, " + cond + "\n";
            code += "  and     " + dest + ", " + tv + ", t0\n";
            if (rd.empty()) code += storeFromReg(&inst, dest);
            emitter.emitText(code);
            return;
        }

        // Pattern C: select cond(0/1), 0, var
        //   cond=1 → dest=0; cond=0 → dest=var
        //   seqz t0, cond  (t0 = !cond; cond is dead after)
        //   neg  t0, t0    (t0 = -!cond = 0 or -1)
        //   and  dest, var, t0
        // Safe: cond is dead after seqz.
        if (trueConst && trueConst->getValue() == 0 && !falseConst) {
            std::string code;
            std::string condReg = getValueReg(inst.getOperand(0));
            std::string falseReg = getValueReg(falseVal);

            std::string cond = condReg;
            if (cond.empty()) { code += loadToReg(inst.getOperand(0), "t0"); cond = "t0"; }
            std::string fv = falseReg;
            if (fv.empty()) { code += loadToReg(falseVal, "t1"); fv = "t1"; }

            code += "  seqz    t0, " + cond + "\n";
            code += "  neg     t0, t0\n";
            code += "  and     " + dest + ", " + fv + ", t0\n";
            if (rd.empty()) code += storeFromReg(&inst, dest);
            emitter.emitText(code);
            return;
        }

        // Pattern B: select cond(0/1), 1, var
        //   cond=1 → dest=1; cond=0 → dest=var
        //   seqz tX, cond; neg tX, tX; and tX, var, tX; or dest, tX, cond
        // CAUTION: cond is needed in the final `or`, so it must survive the seqz.
        // - If cond is in its own register: use t0 for intermediate. cond survives in its reg.
        // - If cond needs loading (→t0): use t1 for intermediate. cond survives in t0.
        // - If BOTH cond and var need loading: only 2 scratch regs, need 3 slots → fall back.
        if (trueConst && trueConst->getValue() == 1 && !falseConst) {
            std::string condReg = getValueReg(inst.getOperand(0));
            std::string falseReg = getValueReg(falseVal);
            bool condNeedsLoad = condReg.empty();
            bool fvNeedsLoad = falseReg.empty();

            if (!(condNeedsLoad && fvNeedsLoad)) {
                std::string code;
                std::string cond, fv, scratch;
                if (condNeedsLoad) {
                    code += loadToReg(inst.getOperand(0), "t0");
                    cond = "t0"; fv = falseReg; scratch = "t1";
                } else {
                    cond = condReg;
                    if (fvNeedsLoad) { code += loadToReg(falseVal, "t1"); fv = "t1"; }
                    else { fv = falseReg; }
                    scratch = "t0";
                }
                code += "  seqz    " + scratch + ", " + cond + "\n";
                code += "  neg     " + scratch + ", " + scratch + "\n";
                code += "  and     " + scratch + ", " + fv + ", " + scratch + "\n";
                code += "  or      " + dest + ", " + scratch + ", " + cond + "\n";
                if (rd.empty()) code += storeFromReg(&inst, dest);
                emitter.emitText(code);
                return;
            }
        }

        // Pattern D: select cond(0/1), var, 1
        //   cond=1 → dest=var; cond=0 → dest=1
        //   neg  tX, cond; and tX, var, tX; xori tY, cond, 1; or dest, tX, tY
        // CAUTION: cond is needed in xori. Same scratch constraint as Pattern B.
        // - If cond in own reg: t0 for mask, t1 for !cond. ✓
        // - If cond needs load (→t0): t1 for mask, t0 still has cond for xori.
        //   But then !cond also needs a reg... xori t1, cond, 1 overwrites t1 (mask).
        //   Need 3 regs: cond(t0), mask, !cond. Only have 2 scratch. Fall back.
        // - If BOTH need loading: fall back.
        if (falseConst && falseConst->getValue() == 1 && !trueConst) {
            std::string condReg = getValueReg(inst.getOperand(0));
            std::string trueReg = getValueReg(trueVal);
            bool condNeedsLoad = condReg.empty();
            bool tvNeedsLoad = trueReg.empty();

            if (!condNeedsLoad && !tvNeedsLoad) {
                // Best case: both in registers, use t0 and t1
                std::string code;
                std::string cond = condReg;
                std::string tv = trueReg;
                code += "  neg     t0, " + cond + "\n";
                code += "  and     t0, " + tv + ", t0\n";
                code += "  xori    t1, " + cond + ", 1\n";
                code += "  or      " + dest + ", t0, t1\n";
                if (rd.empty()) code += storeFromReg(&inst, dest);
                emitter.emitText(code);
                return;
            } else if (!condNeedsLoad && tvNeedsLoad) {
                // cond in reg, tv needs load → t1. Use t0 for mask+!cond carefully.
                std::string code;
                std::string cond = condReg;
                code += loadToReg(trueVal, "t1");  // t1 = tv
                code += "  neg     t0, " + cond + "\n";
                code += "  and     t0, t1, t0\n";   // t0 = tv & -cond
                code += "  xori    t1, " + cond + ", 1\n";  // t1 = !cond (tv dead)
                code += "  or      " + dest + ", t0, t1\n";
                if (rd.empty()) code += storeFromReg(&inst, dest);
                emitter.emitText(code);
                return;
            }
            // condNeedsLoad: fall back (not enough scratch regs)
        }
    }

    // Fallback: branch-based lowering (for float or non-constant-operand integer)
    static int selectLabelCounter = 0;
    int labelId = selectLabelCounter++;
    std::string labelFalse = ".Lselect_false_" + std::to_string(labelId);
    std::string labelEnd   = ".Lselect_end_"   + std::to_string(labelId);

    std::string code;
    std::string condReg = getValueReg(inst.getOperand(0));
    std::string trueReg = getValueReg(inst.getOperand(1));
    std::string falseReg = getValueReg(inst.getOperand(2));

    // 1. Load cond into its register or t0
    std::string cond = condReg;
    if (cond.empty()) {
        code += loadToReg(inst.getOperand(0), "t0");
        cond = "t0";
    }

    // 2. beqz cond, false_label  (cond is dead after this)
    code += "  beqz    " + cond + ", " + labelFalse + "\n";

    // 3. True path: load trueVal and move to dest
    //    cond is dead, so t0 is free for reuse (if cond was in t0)
    std::string tv = trueReg;
    if (tv.empty()) {
        code += loadToReg(inst.getOperand(1), "t0");
        tv = "t0";
    }
    if (dest != tv) {
        if (isFloat) {
            code += "  fmv.s   " + dest + ", " + tv + "\n";
        } else {
            code += "  mv      " + dest + ", " + tv + "\n";
        }
    }
    code += "  j       " + labelEnd + "\n";

    // 4. False path: load falseVal and move to dest
    code += labelFalse + ":\n";
    std::string fv = falseReg;
    if (fv.empty()) {
        code += loadToReg(inst.getOperand(2), "t0");
        fv = "t0";
    }
    if (dest != fv) {
        if (isFloat) {
            code += "  fmv.s   " + dest + ", " + fv + "\n";
        } else {
            code += "  mv      " + dest + ", " + fv + "\n";
        }
    }
    code += labelEnd + ":\n";

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

    // 允许提升 PHI 降低产生的 ALLOCA（.phi.ptr）：
    // 这些 ALLOCA 对应循环变量、累加器等高频使用的值。
    // promoteAllocasInFunction 按使用次数降序排序，只提升使用频率最高的
    // 前 N 个（N = 可用 callee-saved 寄存器数），因此不会耗尽寄存器。
    // 之前的"12_DSU SEGFAULT"是因为没有限制提升数量，现在有限制所以安全。

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

    // 收集所有可提升的 ALLOCA，按使用次数降序排序
    // 这样高频使用的变量（如循环计数器、累加器）优先获得寄存器，
    // 低频使用的变量（如一次性中间值）留在栈上
    struct AllocaCandidate {
        IR::Instruction* alloca;
        size_t useCount;
        bool isFloat;
    };
    std::vector<AllocaCandidate> candidates;

    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::ALLOCA) continue;
            if (!isAllocaPromotable(inst.get())) continue;

            auto* ptrTy = dynamic_cast<IR::PointerType*>(inst->getType());
            auto* pointee = ptrTy->getPointeeType();
            bool isFloat = pointee->isFloat();

            candidates.push_back({inst.get(), inst->getUses().size(), isFloat});
        }
    }

    // 按使用次数降序排序（稳定排序保证相同 useCount 时保持 IR 顺序）
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const AllocaCandidate& a, const AllocaCandidate& b) {
            return a.useCount > b.useCount;
        });

    // 寄存器预算管理：
    //   s0-s11 共 12 个，fs0-fs11 共 12 个
    //   collectGlobalAddresses 已占用部分 s 寄存器缓存全局数组地址
    //   collectLargeConstants 可能占用最多 2 个 s 寄存器缓存大常量
    //   必须为线性扫描分配器保留足够的寄存器，否则会导致大量溢出
    //   策略：int 最多用 8 个（含全局地址+常量缓存），float 最多用 8 个
    //   这样分配器至少有 4 个 s 寄存器 + 4 个 t 寄存器 = 8 个可用
    const int MAX_INT_PROMOTIONS = 8;
    const int MAX_FLOAT_PROMOTIONS = 8;
    int globalIntRegsUsed = static_cast<int>(globalAddrCache.size() + constantCache.size());
    int intPromotionBudget = MAX_INT_PROMOTIONS - globalIntRegsUsed;
    if (intPromotionBudget < 0) intPromotionBudget = 0;

    int intIdx = 0, floatIdx = 0;
    int intPromoted = 0, floatPromoted = 0;

    for (auto& cand : candidates) {
        std::string reg;
        if (cand.isFloat) {
            if (floatPromoted >= MAX_FLOAT_PROMOTIONS) continue;
            while (floatIdx < static_cast<int>(floatRegPool.size()) &&
                   regAlloc.isRegReserved(floatRegPool[floatIdx])) {
                floatIdx++;
            }
            if (floatIdx < static_cast<int>(floatRegPool.size())) {
                reg = floatRegPool[floatIdx++];
                floatPromoted++;
            }
        } else {
            if (intPromoted >= intPromotionBudget) continue;
            while (intIdx < static_cast<int>(intRegPool.size()) &&
                   regAlloc.isRegReserved(intRegPool[intIdx])) {
                intIdx++;
            }
            if (intIdx < static_cast<int>(intRegPool.size())) {
                reg = intRegPool[intIdx++];
                intPromoted++;
            }
        }

        if (!reg.empty()) {
            promotedAllocas[cand.alloca] = reg;
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

    // 为每个数组全局变量分配 callee-saved 寄存器缓存其地址
    // 注意：此时 promotedAllocas 尚未填充（emitFunction 中 collectGlobalAddresses
    // 在 promoteAllocasInFunction 之前调用），所以无需检查 promotedAllocas。
    // 实际的寄存器冲突解决在 promoteAllocasInFunction 中通过 regAlloc.isRegReserved() 完成。
    static const std::vector<std::string> intRegPool = {
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11"
    };

    int regIdx = 0;
    for (auto* gv : usedGlobals) {
        if (regIdx < static_cast<int>(intRegPool.size())) {
            std::string reg = intRegPool[regIdx++];
            globalAddrCache[gv] = reg;
        }
    }
}

void TargetCodeGen::collectLargeConstants(IR::Function& func) {
    constantCache.clear();
    // 统计大常量在 GEP 中出现的次数：
    //   1. 全常量索引 GEP 的总偏移量（> 2047）
    //   2. 变量索引 GEP 的 stride（> 8，无法用 slli 优化）
    std::unordered_map<int64_t, int> offsetCounts;
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::GETELEMENTPTR) continue;
            unsigned numOps = inst->getNumOperands();
            if (numOps < 2) continue;

            auto* ptrTy = dynamic_cast<IR::PointerType*>(inst->getOperand(0)->getType());
            IR::Type* curPointee = ptrTy ? ptrTy->getPointeeType() : nullptr;

            // 第一个索引
            auto* firstIdx = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
            int ptrStride = curPointee ? getTypeSize(curPointee) : 4;
            if (firstIdx) {
                // 常量索引：计算偏移量
                if (firstIdx->getValue() != 0) {
                    int64_t offset = (int64_t)firstIdx->getValue() * ptrStride;
                    if (offset > 2047 || offset < -2048) {
                        offsetCounts[offset]++;
                    }
                }
            } else {
                // 变量索引：stride 本身是大常量时需要缓存
                if (ptrStride > 8) {
                    offsetCounts[ptrStride]++;
                }
            }

            // 后续索引（operand 2+）
            IR::Type* curType = curPointee;
            for (unsigned i = 2; i < numOps; ++i) {
                int stride = 4;
                if (curType && curType->isArray()) {
                    auto* arrTy = dynamic_cast<IR::ArrayType*>(curType);
                    stride = getTypeSize(arrTy->getElementType());
                    curType = arrTy->getElementType();
                }
                auto* idxConst = dynamic_cast<IR::ConstantInt*>(inst->getOperand(i));
                if (idxConst) {
                    if (idxConst->getValue() != 0) {
                        int64_t offset = (int64_t)idxConst->getValue() * stride;
                        if (offset > 2047 || offset < -2048) {
                            offsetCounts[offset]++;
                        }
                    }
                } else {
                    if (stride > 8) {
                        offsetCounts[stride]++;
                    }
                }
            }
        }
    }

    // 也统计 binop 中的大常量操作数（如 mul %i, 5600）
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            using Opc = IR::Instruction::Opcode;
            Opc oc = inst->getOpcode();
            if (oc != Opc::ADD && oc != Opc::SUB && oc != Opc::MUL &&
                oc != Opc::AND && oc != Opc::OR && oc != Opc::XOR) continue;
            unsigned numOps = inst->getNumOperands();
            for (unsigned i = 0; i < numOps && i < 2; ++i) {
                auto* ci = dynamic_cast<IR::ConstantInt*>(inst->getOperand(i));
                if (!ci) continue;
                int64_t val = ci->getValue();
                if (val > 2047 || val < -2048) {
                    offsetCounts[val]++;
                }
            }
        }
    }

    // 按出现次数降序排序，只缓存出现 >= 3 次的常量
    std::vector<std::pair<int64_t, int>> sortedOffsets(offsetCounts.begin(), offsetCounts.end());
    std::sort(sortedOffsets.begin(), sortedOffsets.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    static const std::vector<std::string> intRegPool = {
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11"
    };
    const int MAX_CONST_REGS = 2;  // 限制常量缓存最多用 2 个 s 寄存器
    int allocated = 0;
    for (auto& [val, count] : sortedOffsets) {
        if (allocated >= MAX_CONST_REGS) break;
        if (count < 3) break;
        // 找一个未预留的寄存器
        for (const auto& reg : intRegPool) {
            if (!regAlloc.isRegReserved(reg)) {
                constantCache[val] = reg;
                regAlloc.reserveReg(reg);
                ++allocated;
                break;
            }
        }
    }
}

} // namespace Backend
