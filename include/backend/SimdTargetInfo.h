#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Backend {

// SIMD is deliberately opt-in.  The default target remains the scalar
// RV64GC configuration until the final target specification is available.
enum class SimdMode : std::uint8_t {
    Off,
    Analyze,
    On,
};

// A scalable shape is used by ISAs whose vector length is selected at run
// time (for example RVV).  FixedWidth describes packed SIMD with a fixed
// number of bits per vector.  Disabled means that no target has been bound.
enum class SimdShape : std::uint8_t {
    Disabled,
    FixedWidth,
    Scalable,
};

enum class SimdElementType : std::uint8_t {
    I32,
    I64,
    F32,
    F64,
};

enum class SimdOperation : std::uint8_t {
    Load,
    Store,
    Broadcast,
    Add,
    Sub,
    Mul,
    Div,
    Rem,
    Compare,
    ReduceAdd,
    Fma,
    Gather,
    Scatter,
};

constexpr std::size_t kSimdElementTypeCount = 4;
constexpr std::size_t kSimdOperationCount = 13;

enum class SimdMemoryForm : std::uint8_t {
    None,
    Contiguous,
    Strided,
    GatherScatter,
};

constexpr std::size_t kSimdMemoryFormCount = 4;

constexpr std::size_t simdElementTypeIndex(SimdElementType type) noexcept {
    return static_cast<std::size_t>(type);
}

constexpr std::size_t simdOperationIndex(SimdOperation operation) noexcept {
    return static_cast<std::size_t>(operation);
}

constexpr std::size_t simdMemoryFormIndex(SimdMemoryForm form) noexcept {
    return static_cast<std::size_t>(form);
}

constexpr std::uint32_t simdElementBitWidth(
    SimdElementType type) noexcept {
    switch (type) {
    case SimdElementType::I32:
    case SimdElementType::F32:
        return 32;
    case SimdElementType::I64:
    case SimdElementType::F64:
        return 64;
    }
    return 0;
}

// Target facts supplied by an official ISA/board description.  This type is
// intentionally independent of IR and of any particular instruction syntax.
// A zero width or Disabled shape means that no target capability has been
// configured yet.
struct TargetSimdCaps {
    SimdMode mode = SimdMode::Off;
    SimdShape shape = SimdShape::Disabled;
    std::uint32_t fixedVectorBits = 0;
    std::uint32_t vlenBits = 0;
    std::uint32_t elenBits = 0;
    // Bit masks supplied by the official target description. Bit N in
    // supportedSewMask denotes SEW=N; supportedLmulMask uses the documented
    // encoding for fractional and integer LMUL values. Zero means unknown.
    std::uint32_t supportedSewMask = 0;
    std::uint16_t supportedLmulMask = 0;
    std::uint32_t vectorRegisterCount = 0;
    bool abiDefined = false;
    bool supportsMask = false;
    bool supportsTail = false;

    // A capability is present only when all three dimensions are declared.
    // Memory operations without an explicit form are never legal; Gather and
    // Scatter specifically require SimdMemoryForm::GatherScatter.
    std::array<std::array<std::array<bool, kSimdMemoryFormCount>,
                          kSimdOperationCount>,
               kSimdElementTypeCount> capabilities{};

    static TargetSimdCaps disabled() noexcept;
    static TargetSimdCaps unconfigured(SimdMode mode) noexcept;
    static TargetSimdCaps fixedWidth(SimdMode mode,
                                     std::uint32_t vectorBits) noexcept;
    static TargetSimdCaps scalable(SimdMode mode,
                                   std::uint32_t vlenBits,
                                   std::uint32_t elenBits) noexcept;

    bool isConfigured() const noexcept;
    bool isEnabled() const noexcept;
    bool supports(SimdElementType type, SimdOperation operation,
                  SimdMemoryForm memoryForm = SimdMemoryForm::None) const noexcept;

    void enableCapability(SimdElementType type, SimdOperation operation,
                          SimdMemoryForm memoryForm = SimdMemoryForm::None) noexcept;
};

// Only these three values are accepted.  Invalid or unset values are Off so
// a typo cannot silently enable target-specific code generation.
SimdMode parseSimdMode(std::string_view value) noexcept;
SimdMode simdModeFromEnvironment(
    const char* variableName = "SIMD_MODE") noexcept;

// Single binding point for the official final-round target description.
// Until that description is available this preserves the requested mode but
// deliberately returns an unconfigured target.
TargetSimdCaps targetSimdCapsForCurrentBuild(SimdMode mode) noexcept;

}  // namespace Backend
