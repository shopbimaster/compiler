#pragma once

#include "ir/IR.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace Backend {

struct LiveInterval {
    IR::Value* value;
    int start;
    int end;
    bool isFloat;
    std::string reg;
    int spillSlot;
    int useCount;     // number of times this value is used as an operand
    int loopDepth;    // maximum loop nesting depth (0 = outside all loops)
};

class RegisterAllocator {
public:
    RegisterAllocator();

    void allocate(IR::Function& func);
    // K1+K2: 重建 usedCalleeSaved，移除 coalescePhis 释放的寄存器 (K1)
    //        和被代码生成器折叠的 GEP/ICMP 指令的寄存器 (K2)
    void pruneUnusedCalleeSaved(const std::set<IR::Instruction*>& deadInsts);

    void reserveReg(const std::string& reg);
    void clearReservedRegs();
    bool isRegReserved(const std::string& reg) const;

    bool hasReg(IR::Value* val) const;
    std::string getReg(IR::Value* val) const;
    void setReg(IR::Value* val, const std::string& reg);
    int getSpillSlot(IR::Value* val) const;
    bool isFloatValue(IR::Value* val) const;
    const std::vector<std::string>& getUsedCalleeSaved() const;
    int getTotalSpillSize() const;
    // Returns registers holding values live across the given call instruction.
    // Only values whose live interval spans the call (defined before, used after)
    // are included. This avoids saving/restoring caller-saved registers that
    // aren't actually live at this call site.
    std::vector<std::string> getRegsLiveAtCall(IR::Instruction* callInst) const;

private:
    int assignInstructionIds(IR::Function& func);
    void buildIntervals(IR::Function& func);
    void linearScan();
    void expireOldIntervals(int pos, std::vector<LiveInterval*>& active);
    void spillAtInterval(LiveInterval& current, std::vector<LiveInterval*>& active);
    void coalescePhis(IR::Function& func);

    // ── 图着色寄存器分配（Chaitin-Briggs，实验性，RA_ALLOCATOR=graph 启用）──
    // 复用 linearScan 相同的 intervals 输入，只替换“分配决策”这一层：
    // 用全局干涉图 + 溢出代价函数选择溢出对象，弥补线性扫描“贪心、无全局视野”
    // 在 GVN 拉长活跃区间后热循环抖动的缺陷。输出接口（regMap/usedCalleeSaved/…）
    // 与线性扫描完全一致，codegen 无感。
    void colorAllocate();
    bool colorRegClass(bool isFloat);   // 对单一寄存器类做一次完整着色，返回是否全部着色成功

    static bool useGraphColoring();     // 图着色开关：默认启用，RA_ALLOCATOR=linear 切回线扫

    // ── 通用 move coalescing 支持 ──
    // intervalsOverlap: 基于已构造（且经 liveness extension 保守放大）的活跃区间，
    //   判断两个值是否可能同时活跃。区间只会放大不会缩小，因此“不重叠”判定是
    //   保守正确的——可能漏合并，但绝不会错合并。
    // regBusyDuring: 判断物理寄存器 reg 在给定区间内是否被“第三方”值占用。
    // valToInterval: 值 → intervals 元素指针。⚠️ 不变量：buildIntervals 完成后
    //   intervals 不得再 push_back（否则指针失效）；仅允许原地修改元素的 reg 字段。
    bool intervalsOverlap(IR::Value* a, IR::Value* b) const;
    bool regBusyDuring(const std::string& reg,
                       const LiveInterval* range, IR::Value* self) const;
    LiveInterval* intervalOf(IR::Value* v);
    // 经典 read-modify-write 合并判据（旧版已验证安全逻辑），作为独立接受路径。
    bool isClassicRmwCoalesce(IR::Value* incoming, IR::Instruction* phi,
                              const std::string& phiReg) const;

    std::unordered_map<IR::Value*, LiveInterval*> valToInterval;

    std::vector<LiveInterval> intervals;
    std::unordered_map<IR::Instruction*, int> instId;
    int maxInstId;

    std::unordered_map<IR::Value*, std::string> regMap;
    std::unordered_map<IR::Value*, int> spillMap;
    std::unordered_set<IR::Value*> floatValues;
    int nextSpillSlot;
    int spillSlotSize;

    std::vector<std::string> usedCalleeSaved;
    std::unordered_set<std::string> reservedRegs;

    static const std::vector<std::string> INT_REGS;
    static const std::vector<std::string> FLOAT_REGS;
};

} // namespace Backend