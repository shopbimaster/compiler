#pragma once

#include "backend/SimdTargetInfo.h"

#include <string>
#include <vector>

namespace Backend {

struct SimdInstructionRequest {
    SimdOperation operation = SimdOperation::Load;
    SimdElementType elementType = SimdElementType::I32;
    SimdMemoryForm memoryForm = SimdMemoryForm::None;
    std::vector<std::string> operands;
};

struct SimdEmissionResult {
    bool success = false;
    std::string assembly;
    std::string reason;

    static SimdEmissionResult failure(std::string reason);
};

// VectorPlan-independent emission boundary. The pre-spec implementation is
// deliberately fail-closed: it never guesses an opcode or encoding.
class SimdEmitter {
public:
    explicit SimdEmitter(TargetSimdCaps caps = TargetSimdCaps::disabled());
    virtual ~SimdEmitter() = default;

    const TargetSimdCaps& capabilities() const noexcept;
    bool canEmit(const SimdInstructionRequest& request) const noexcept;
    virtual SimdEmissionResult emit(
        const SimdInstructionRequest& request) const;

private:
    TargetSimdCaps caps;
};

} // namespace Backend
