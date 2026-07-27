#pragma once

#include <string>

namespace Backend {

// 对最终汇编文本做寄存器分配后的局部指令调度（延迟感知，隐藏 load-use 延迟）。
// 纯文本后处理：逐块解析 def/use，构建依赖 DAG，按关键路径重排。
// 未识别助记符或涉及 sp/ra 的块整块跳过，保证正确性。SCHED_OFF=1 关闭。
std::string postRASchedule(const std::string& asmText);

} // namespace Backend
