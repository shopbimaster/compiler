#include "backend/SimdEmitter.h"

#include <cstdlib>
#include <utility>

namespace Backend {
namespace {

bool equalsAsciiInsensitive(
    std::string_view value, std::string_view expected) noexcept {
    if (value.size() != expected.size()) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        char lhs = value[i];
        char rhs = expected[i];
        if (lhs >= 'A' && lhs <= 'Z') lhs = static_cast<char>(lhs - 'A' + 'a');
        if (rhs >= 'A' && rhs <= 'Z') rhs = static_cast<char>(rhs - 'A' + 'a');
        if (lhs != rhs) return false;
    }
    return true;
}

}  // namespace

TargetSimdCaps TargetSimdCaps::disabled() noexcept {
    return TargetSimdCaps{};
}

TargetSimdCaps TargetSimdCaps::unconfigured(SimdMode requestedMode) noexcept {
    TargetSimdCaps caps;
    caps.mode = requestedMode;
    return caps;
}

TargetSimdCaps TargetSimdCaps::fixedWidth(SimdMode requestedMode,
                                          std::uint32_t vectorBits) noexcept {
    TargetSimdCaps caps;
    if (vectorBits == 0 || requestedMode == SimdMode::Off) {
        return caps;
    }
    caps.mode = requestedMode;
    caps.shape = SimdShape::FixedWidth;
    caps.fixedVectorBits = vectorBits;
    return caps;
}

TargetSimdCaps TargetSimdCaps::scalable(SimdMode requestedMode,
                                        std::uint32_t requestedVlenBits,
                                        std::uint32_t requestedElenBits) noexcept {
    TargetSimdCaps caps;
    if (requestedVlenBits == 0 || requestedElenBits == 0 ||
        requestedMode == SimdMode::Off) {
        return caps;
    }
    caps.mode = requestedMode;
    caps.shape = SimdShape::Scalable;
    caps.vlenBits = requestedVlenBits;
    caps.elenBits = requestedElenBits;
    return caps;
}

bool TargetSimdCaps::isConfigured() const noexcept {
    if (mode == SimdMode::Off || shape == SimdShape::Disabled) {
        return false;
    }
    if (shape == SimdShape::FixedWidth) {
        return fixedVectorBits != 0;
    }
    return vlenBits != 0 && elenBits != 0;
}

bool TargetSimdCaps::isEnabled() const noexcept {
    return mode == SimdMode::On && isConfigured();
}

bool TargetSimdCaps::supports(SimdElementType type,
                              SimdOperation operation,
                              SimdMemoryForm memoryForm) const noexcept {
    const std::size_t typeIndex = simdElementTypeIndex(type);
    const std::size_t operationIndex = simdOperationIndex(operation);
    const std::size_t memoryIndex = simdMemoryFormIndex(memoryForm);
    const std::uint32_t elementBits = simdElementBitWidth(type);
    if (!isConfigured() || typeIndex >= kSimdElementTypeCount ||
        operationIndex >= kSimdOperationCount ||
        memoryIndex >= kSimdMemoryFormCount || elementBits == 0) {
        return false;
    }
    const bool isMemoryOperation =
        operation == SimdOperation::Load || operation == SimdOperation::Store ||
        operation == SimdOperation::Gather ||
        operation == SimdOperation::Scatter;
    if ((isMemoryOperation && memoryForm == SimdMemoryForm::None) ||
        (!isMemoryOperation && memoryForm != SimdMemoryForm::None)) {
        return false;
    }
    if ((operation == SimdOperation::Gather ||
         operation == SimdOperation::Scatter) &&
        memoryForm != SimdMemoryForm::GatherScatter) {
        return false;
    }
    if (shape == SimdShape::FixedWidth && elementBits > fixedVectorBits) {
        return false;
    }
    if (shape == SimdShape::Scalable && elementBits > elenBits) {
        return false;
    }
    return capabilities[typeIndex][operationIndex][memoryIndex];
}

void TargetSimdCaps::enableCapability(
    SimdElementType type, SimdOperation operation,
    SimdMemoryForm memoryForm) noexcept {
    const std::size_t typeIndex = simdElementTypeIndex(type);
    const std::size_t operationIndex = simdOperationIndex(operation);
    const std::size_t memoryIndex = simdMemoryFormIndex(memoryForm);
    if (typeIndex >= kSimdElementTypeCount ||
        operationIndex >= kSimdOperationCount ||
        memoryIndex >= kSimdMemoryFormCount) {
        return;
    }
    const bool isMemoryOperation =
        operation == SimdOperation::Load || operation == SimdOperation::Store ||
        operation == SimdOperation::Gather ||
        operation == SimdOperation::Scatter;
    if ((isMemoryOperation && memoryForm == SimdMemoryForm::None) ||
        (!isMemoryOperation && memoryForm != SimdMemoryForm::None)) {
        return;
    }
    if ((operation == SimdOperation::Gather ||
         operation == SimdOperation::Scatter) &&
        memoryForm != SimdMemoryForm::GatherScatter) {
        return;
    }
    capabilities[typeIndex][operationIndex][memoryIndex] = true;
}

SimdMode parseSimdMode(std::string_view value) noexcept {
    if (equalsAsciiInsensitive(value, "analyze")) {
        return SimdMode::Analyze;
    }
    if (equalsAsciiInsensitive(value, "on")) {
        return SimdMode::On;
    }
    return SimdMode::Off;
}

SimdMode simdModeFromEnvironment(const char* variableName) noexcept {
    if (variableName == nullptr || *variableName == '\0') {
        return SimdMode::Off;
    }
    const char* value = std::getenv(variableName);
    return value == nullptr ? SimdMode::Off : parseSimdMode(value);
}

TargetSimdCaps targetSimdCapsForCurrentBuild(SimdMode mode) noexcept {
    return TargetSimdCaps::unconfigured(mode);
}

SimdEmissionResult SimdEmissionResult::failure(std::string reason) {
    SimdEmissionResult result;
    result.reason = std::move(reason);
    return result;
}

SimdEmitter::SimdEmitter(TargetSimdCaps targetCaps)
    : caps(std::move(targetCaps)) {}

const TargetSimdCaps& SimdEmitter::capabilities() const noexcept {
    return caps;
}

bool SimdEmitter::canEmit(const SimdInstructionRequest& request) const noexcept {
    // Capability data is useful to an analyzer, but this implementation has
    // no ISA-specific encoder.  Keep this false until one is explicitly
    // supplied; this prevents accidental opcode emission before the official
    // target document is available.
    (void)request;
    return false;
}

SimdEmissionResult SimdEmitter::emit(
    const SimdInstructionRequest& request) const {
    if (caps.mode == SimdMode::Off) {
        return SimdEmissionResult::failure("SIMD mode is off");
    }
    if (caps.mode == SimdMode::Analyze) {
        return SimdEmissionResult::failure("SIMD analysis mode does not emit code");
    }
    if (!caps.isConfigured()) {
        return SimdEmissionResult::failure(
            "SIMD target capabilities are not configured");
    }
    if (!caps.supports(request.elementType, request.operation,
                       request.memoryForm)) {
        return SimdEmissionResult::failure(
            "SIMD operation is not declared by the target capabilities");
    }
    return SimdEmissionResult::failure(
        "no ISA-specific SIMD emitter is registered");
}

}  // namespace Backend
