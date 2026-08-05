#pragma once

#include "ir/IR.h"
#include "backend/RegisterAllocator.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sstream>

namespace Backend {

enum class Register {
    x0, ra, sp, gp, tp, t0, t1, t2,
    s0, s1, a0, a1, a2, a3, a4, a5,
    a6, a7, s2, s3, s4, s5, s6, s7,
    s8, s9, s10, s11, t3, t4, t5, t6
};

std::string regToString(Register reg);

class CodeEmitter {
public:
    void emitText(const std::string& asmLine) { text << asmLine << std::endl; }
    void emitData(const std::string& asmLine) { data << asmLine << std::endl; }
    std::string getTextSection() const { return text.str(); }
    std::string getDataSection() const { return data.str(); }

private:
    std::ostringstream text;
    std::ostringstream data;
};

class TargetCodeGen {
public:
    TargetCodeGen();
    std::string generate(IR::Module& module);

private:
    CodeEmitter emitter;
    IR::Function* currentFunc = nullptr;
    IR::BasicBlock* currentBB = nullptr;
    // Fall-through 优化：当前块物理上的下一个块（BOOM 分支友好布局）
    // 当 BR/COND_BR 的跳转目标恰好是 nextBB 时，省掉冗余 j 指令。
    IR::BasicBlock* nextBB = nullptr;
    bool nextIsExit = false;  // 当前块是否为最后一块（其后紧接 func_exit 标签）
    int stackSize;
    int labelCounter;
    bool savesRA = false;  // true if function contains CALL instructions

    std::unordered_map<IR::Value*, int> vregStackOffset;
    std::unordered_map<IR::Value*, int> allocaOffset;
    std::unordered_map<IR::Argument*, int> paramOffsets;
    RegisterAllocator regAlloc;

    // ALLOCA 寄存器提升：将符合条件的 ALLOCA 映射到物理寄存器
    std::unordered_map<IR::Value*, std::string> promotedAllocas;
    void promoteAllocasInFunction(IR::Function& func);
    bool isAllocaPromotable(IR::Instruction* alloca) const;

    // 全局变量地址缓存：避免循环内重复 la 指令
    std::unordered_map<IR::GlobalVariable*, std::string> globalAddrCache;
    void collectGlobalAddresses(IR::Function& func);

    // 大常量缓存：GEP 中频繁使用的大偏移量（> 2047，无法用 addi 立即数）
    // 缓存到 callee-saved 寄存器中，避免每次 GEP 都发射 li 指令
    // 典型场景：多维数组的行步长（如 [1400 x i32] 的行步长 5600）
    std::unordered_map<int64_t, std::string> constantCache;
    void collectLargeConstants(IR::Function& func);

    // FoldMemoryAccess：将 GETELEMENTPTR+LOAD/STORE 融合为单次地址计算+内存访问
    std::unordered_set<IR::Instruction*> foldedGeps;
    void collectFoldedGeps(IR::Function& func);
    std::string emitGEPAddressToReg(IR::Instruction& gep, const std::string& addrReg);

    // Icmp+CondBr 融合：当 ICMP 结果只被单个 COND_BR 使用时，
    // 跳过 ICMP 结果计算，直接生成分支指令 (beq/bne/blt/bge)
    std::unordered_set<IR::Instruction*> inlinedIcmps;
    void collectInlinedIcmps(IR::Function& func);

    // 循环头对齐：检测回边目标 BB，在汇编中对齐以优化 icache 取指
    std::unordered_set<IR::BasicBlock*> loopHeaders;
    void detectLoopHeaders(IR::Function& func);

    // G2 指令预取（Zicbop prefetch.i）：在循环 preheader 块首预取循环体地址，
    // 每进入循环一次预取一次，改善 BOOM I-cache 局部性。
    // 映射：preheader 块 → 预取目标（循环头）块。
    std::unordered_map<IR::BasicBlock*, IR::BasicBlock*> prefetchTargetMap;
    void detectPrefetchSites(IR::Function& func);

    // PHI 消除：直接在前驱块中插入寄存器拷贝，避免通过内存传递
    // 使用 (前驱, 后继) 边作为键，确保 COND_BR 的不同分支只发射各自的 PHI moves
    struct PhiMove {
        IR::Instruction* phi;       // PHI 指令（结果值）
        IR::Value* incoming;        // 来自当前前驱的输入值
    };
    struct EdgeKey {
        IR::BasicBlock* from;
        IR::BasicBlock* to;
        bool operator==(const EdgeKey& o) const { return from == o.from && to == o.to; }
    };
    struct EdgeKeyHash {
        size_t operator()(const EdgeKey& k) const {
            return std::hash<void*>{}(k.from) ^ (std::hash<void*>{}(k.to) << 1);
        }
    };
    std::unordered_map<EdgeKey, std::vector<PhiMove>, EdgeKeyHash> phiMoveMap;
    void buildPhiMoveMap(IR::Function& func);
    void emitPhiMovesForEdge(IR::BasicBlock* from, IR::BasicBlock* to);
    std::string emitValueToStack(IR::Value* val, int stackOffset, bool isFloat);

    void emitGlobal(IR::GlobalVariable* gv);
    void emitGlobalInitData(IR::Constant* init, IR::Type* type, const std::string& indent);

    void emitFunction(IR::Function& func);
    void emitBasicBlock(IR::BasicBlock& bb);
    void emitInstruction(IR::Instruction& inst);

    void computeStackLayout(IR::Function& func);
    int getTypeSize(IR::Type* t);
    int getStackOffset(IR::Value* val);
    int allocSlot(IR::Value* val);

    void emitPrologue(IR::Function& func);
    void emitEpilogue(IR::Function& func);

    void emitRet(IR::Instruction& inst);
    void emitBr(IR::Instruction& inst);
    void emitCondBr(IR::Instruction& inst);
    void emitBinOp(IR::Instruction& inst);
    void emitWideSmodMul(IR::Instruction& inst);
    void emitFBinOp(IR::Instruction& inst);
    void emitIcmp(IR::Instruction& inst);
    void emitFcmp(IR::Instruction& inst);
    void emitLoad(IR::Instruction& inst);
    void emitStore(IR::Instruction& inst);
    void emitCall(IR::Instruction& inst);
    void emitGetElementPtr(IR::Instruction& inst);
    void emitSitofp(IR::Instruction& inst);
    void emitFptosi(IR::Instruction& inst);
    void emitCast(IR::Instruction& inst);
    void emitSelect(IR::Instruction& inst);

    std::string loadToReg(IR::Value* val, const std::string& destReg);
    std::string storeFromReg(IR::Value* val, const std::string& srcReg, bool addNew = false);
    std::string getValueReg(IR::Value* val);
    std::string emitGlobalAddr(IR::GlobalVariable* gv, const std::string& destReg);
    std::string emitValueToReg(IR::Value* val, const std::string& destReg);

    // 获取值所在寄存器（如果已在寄存器中），用于 GEP 寻址优化
    // 同时检查 regAlloc 和 globalAddrCache，返回空字符串表示不在寄存器中
    std::string getValueRegIfAny(IR::Value* val);

    // Helpers for large stack frames exceeding RISC-V 12-bit immediate
    static bool fitsImm12(int val) { return val >= -2048 && val <= 2047; }
    std::string emitStackLoad(const std::string& reg, int offset, const std::string& insn);
    std::string emitStackStore(const std::string& reg, int offset, const std::string& insn);
    std::string emitSPAddImm(int delta);

    // 生成 stride 乘法代码：将 srcReg 乘以 stride，结果留在 srcReg
    // 优化：stride 为 2 的幂次时使用 slli（1 cycle），否则使用 mul（3 cycle）
    // largeConstReg 用于非 2 的幂次大 stride 的 li 临时寄存器
    std::string emitStrideMul(const std::string& srcReg, int stride,
                              const std::string& largeConstReg);
};

}
