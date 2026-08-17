#include "Compiler.h"
#include "backend/SimdEmitter.h"
#include "backend/TargetCodeGen.h"
#include "opt/Optimizer.h"
#include "opt/VectorPlan.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace IR {

static void runOptPasses(Module* mod, OptLevel opt) {
    switch (opt) {
    case OptLevel::O1:
        Opt::runO1(mod);
        break;
    case OptLevel::O2:
        Opt::runO1(mod);
        Opt::runO2(mod);
        break;
    case OptLevel::O3:
        Opt::runO1(mod);
        Opt::runO2(mod);
        Opt::runO3(mod);
        break;
    case OptLevel::OALL:
        // OALL = O1+O2+O3+P0，对应命令行 -O1（测评服务器唯一支持的优化选项）
        Opt::runO1(mod);
        Opt::runO2(mod);
        Opt::runO3(mod);
        Opt::runP0(mod);
        Opt::runP3(mod);  // 指令调度
        break;
    case OptLevel::O0:
    default:
        break;
    }
}

static Backend::SimdMode runSimdPlanning(Module* mod) {
    const Backend::SimdMode mode = Backend::simdModeFromEnvironment();
    if (mode == Backend::SimdMode::Off) return mode;

    const auto reports = Opt::analyzeVectorizationCandidates(mod);
    std::size_t accepted = 0;
    for (const auto& report : reports) {
        if (report.accepted()) ++accepted;
        std::cerr << "[simd] " << Opt::formatVectorizationReport(report)
                  << '\n';
    }
    std::cerr << "[simd] candidates=" << accepted
              << ", loops=" << reports.size() << '\n';

    if (mode != Backend::SimdMode::On) return mode;

    const Backend::TargetSimdCaps caps =
        Backend::targetSimdCapsForCurrentBuild(mode);
    const Backend::SimdEmitter emitter(caps);
    if (!emitter.capabilities().isConfigured()) {
        std::cerr << "[simd] target capabilities are not configured; "
                     "using scalar fallback\n";
        return mode;
    }

    std::cerr << "[simd] no ISA-specific emitter is registered; "
                 "using scalar fallback\n";
    return mode;
}

std::unique_ptr<Module> Compiler::compile(const std::string& sourcePath) {
    IRBuilder builder;
    return builder.compile(sourcePath);
}

void Compiler::emitIR(const std::string& sourcePath, std::ostream& out, OptLevel opt) {
    auto mod = compile(sourcePath);
    runOptPasses(mod.get(), opt);
    out << mod->dump();
}

void Compiler::emitIRToFile(const std::string& sourcePath, const std::string& outputPath, OptLevel opt) {
    auto mod = compile(sourcePath);
    runOptPasses(mod.get(), opt);
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("Cannot open output file: " + outputPath);
    }
    ofs << mod->dump();
}

void Compiler::emitAsm(const std::string& sourcePath, std::ostream& out, OptLevel opt) {
    auto mod = compile(sourcePath);
    runOptPasses(mod.get(), opt);
    const auto simdMode = runSimdPlanning(mod.get());
    if (const char* lowerPhi = std::getenv("DEBUG_LOWER_PHI")) {
        if (std::string(lowerPhi) == "1") Opt::phiLowering(mod.get());
    }
    // PHI 指令由 TargetCodeGen 的 emitPhiMovesForEdge 直接在前驱块中
    // 发射寄存器拷贝，无需 phiLowering 降级为 alloca/store/load
    Backend::TargetCodeGen cg(
        Backend::targetSimdCapsForCurrentBuild(simdMode));
    std::string asmCode = cg.generate(*mod);
    out << Opt::peepholeOptimize(asmCode);
}

void Compiler::emitAsmToFile(const std::string& sourcePath, const std::string& outputPath, OptLevel opt) {
    auto mod = compile(sourcePath);
    runOptPasses(mod.get(), opt);
    const auto simdMode = runSimdPlanning(mod.get());
    if (const char* lowerPhi = std::getenv("DEBUG_LOWER_PHI")) {
        if (std::string(lowerPhi) == "1") Opt::phiLowering(mod.get());
    }
    // PHI 指令由 TargetCodeGen 的 emitPhiMovesForEdge 直接在前驱块中
    // 发射寄存器拷贝，无需 phiLowering 降级为 alloca/store/load
    Backend::TargetCodeGen cg(
        Backend::targetSimdCapsForCurrentBuild(simdMode));
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("Cannot open output file: " + outputPath);
    }
    std::string asmCode = cg.generate(*mod);
    const char* noPeep = std::getenv("NO_PEEPHOLE");
    if (noPeep && std::string(noPeep) == "1") {
        ofs << asmCode;
    } else {
        const char* dumpPre = std::getenv("DUMP_PEEPHOLE_PRE");
        if (dumpPre) {
            std::ofstream pre("/tmp/peep_pre.S");
            pre << asmCode;
        }
        std::string optimized = Opt::peepholeOptimize(asmCode);
        const char* dumpPost = std::getenv("DUMP_PEEPHOLE_POST");
        if (dumpPost) {
            std::ofstream post("/tmp/peep_post.S");
            post << optimized;
        }
        ofs << optimized;
    }
}

} // namespace IR
