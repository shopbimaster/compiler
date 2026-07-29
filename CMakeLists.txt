cmake_minimum_required(VERSION 3.20)
project(SysY2022Compiler VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ================================================================
# ANTLR4 C++ Runtime
#
# Generated parser sources use the ANTLR 4.13.1 C++ API (std::any,
# SerializedATNView and ParserATNSimulatorOptions).  Ubuntu's distro package
# can be older, so verify the API explicitly instead of accidentally linking
# an incompatible runtime and failing later with hundreds of compiler errors.
# ================================================================
find_path(ANTLR4_RUNTIME_INCLUDE_DIR
    NAMES antlr4-runtime.h
    PATH_SUFFIXES antlr4-runtime
)
find_library(ANTLR4_RUNTIME_LIBRARY NAMES antlr4-runtime)

if(NOT ANTLR4_RUNTIME_INCLUDE_DIR OR NOT ANTLR4_RUNTIME_LIBRARY)
    message(FATAL_ERROR
        "ANTLR4 C++ runtime 4.13.1 not found. Run scripts/setup/setup-ubuntu.sh."
    )
endif()

if(NOT EXISTS "${ANTLR4_RUNTIME_INCLUDE_DIR}/atn/SerializedATNView.h" OR
   NOT EXISTS "${ANTLR4_RUNTIME_INCLUDE_DIR}/atn/ParserATNSimulatorOptions.h")
    message(FATAL_ERROR
        "Incompatible ANTLR4 C++ runtime at ${ANTLR4_RUNTIME_INCLUDE_DIR}. "
        "Generated sources require 4.13.1; run scripts/setup/setup-ubuntu.sh."
    )
endif()

add_library(antlr4_runtime_system UNKNOWN IMPORTED)
set_target_properties(antlr4_runtime_system PROPERTIES
    IMPORTED_LOCATION "${ANTLR4_RUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ANTLR4_RUNTIME_INCLUDE_DIR}"
)
set(ANTLR4_RUNTIME_TARGET antlr4_runtime_system)

message(STATUS "ANTLR4 runtime headers: ${ANTLR4_RUNTIME_INCLUDE_DIR}")
message(STATUS "ANTLR4 runtime library: ${ANTLR4_RUNTIME_LIBRARY}")

set(ANTLR_GENERATED_DIR ${CMAKE_SOURCE_DIR}/src/antlr)

set(ANTLR_SOURCES
    ${ANTLR_GENERATED_DIR}/SysY2022Lexer.cpp
    ${ANTLR_GENERATED_DIR}/SysY2022Parser.cpp
    ${ANTLR_GENERATED_DIR}/SysY2022ParserBaseVisitor.cpp
    ${ANTLR_GENERATED_DIR}/SysY2022ParserVisitor.cpp
    ${ANTLR_GENERATED_DIR}/SysY2022ParserBaseListener.cpp
    ${ANTLR_GENERATED_DIR}/SysY2022ParserListener.cpp
)

set(ANTLR_INCLUDES
    ${ANTLR_GENERATED_DIR}
)

# ================================================================
# IR 核心库（纯数据结构，不依赖 ANTLR）
# ================================================================
add_library(sysy_ir_core STATIC
    src/ir/IR.cpp
)
target_include_directories(sysy_ir_core PUBLIC include)

# ================================================================
# IRBuilder 库（依赖 ANTLR）
# ================================================================
add_library(sysy_ir_builder STATIC
    src/ir/IRBuilder.cpp
)
target_include_directories(sysy_ir_builder PUBLIC include ${ANTLR_INCLUDES})
target_link_libraries(sysy_ir_builder PUBLIC sysy_ir_core ${ANTLR4_RUNTIME_TARGET})

# ================================================================
# Compiler 封装库
# ================================================================
add_library(sysy_compiler STATIC
    src/Compiler.cpp
)
target_include_directories(sysy_compiler PUBLIC include)
target_link_libraries(sysy_compiler PUBLIC sysy_ir_builder)

# ================================================================
# 后端代码生成库
# ================================================================
add_library(sysy_backend STATIC
    src/backend/TargetCodeGen.cpp
    src/backend/RegisterAllocator.cpp
    src/backend/PostRAScheduler.cpp
)
target_include_directories(sysy_backend PUBLIC include)
target_link_libraries(sysy_backend PUBLIC sysy_ir_core)

# ================================================================
# O1 + O2 + O3 优化库
# ================================================================
add_library(sysy_opt STATIC
    src/opt/Optimizer.cpp
    src/opt/DominatorAnalysis.cpp
    src/opt/ConstantFolding.cpp
    src/opt/DeadCodeElimination.cpp
    src/opt/DeadStoreElimination.cpp
    src/opt/ReadOnlyGlobal.cpp
    src/opt/SCCP.cpp
    src/opt/LoopFind.cpp
    src/opt/AffineRecurrenceAnalysis.cpp
    src/opt/LoopPatternAnalysis.cpp
    src/opt/MemoryAccessAnalysis.cpp
    src/opt/ScalarReductionAnalysis.cpp
    src/opt/SCEVAnalysis.cpp
    src/opt/LoopStrengthReduce.cpp
    src/opt/LoopFullUnroll.cpp
    src/opt/GEPStrengthReduce.cpp
    src/opt/LoadElimination.cpp
    src/opt/Reassociate.cpp
    src/opt/TreeShaking.cpp
    src/opt/CodeSink.cpp
    src/opt/IfConversion.cpp
    src/opt/InstCombine.cpp
    src/opt/SimplifyCFG.cpp
    src/opt/CopyPropagation.cpp
    src/opt/MagicDivision.cpp
    src/opt/BasicBlockReordering.cpp
    src/opt/JumpThreading.cpp
    src/opt/ADCE.cpp
    src/opt/PeepholeOptimizer.cpp
    src/opt/InlineExpansion.cpp
    src/opt/CSE.cpp
    src/opt/LICM.cpp
    src/opt/AlgebraicSimplification.cpp
    src/opt/LoopUnrolling.cpp
    src/opt/LoopInterchange.cpp
    src/opt/TailRecursionElimination.cpp
    src/opt/InstructionScheduling.cpp
    src/opt/RecursiveMulToNative.cpp
    src/opt/RepeatedDivRemToNative.cpp
    src/opt/ModAddRecurrence.cpp
    src/opt/StencilInteriorSpecialization.cpp
    src/opt/RecursiveCallGuard.cpp
    src/opt/MatrixReductionContraction.cpp
    src/opt/MatrixReductionTransform.cpp
    src/opt/RadixSortLowering.cpp
    src/opt/BitOpPatternRecognition.cpp
    src/opt/PowerOfTwoDispatch.cpp
    src/opt/GlobalVariablePromotion.cpp
    src/opt/GlobalConstantPropagation.cpp
    src/opt/Mem2Reg.cpp
    src/opt/GVN.cpp
    src/opt/PhiLowering.cpp
    src/opt/PassManager.cpp
    src/opt/PureFuncDetection.cpp
)
target_include_directories(sysy_opt PUBLIC include)
target_link_libraries(sysy_opt PUBLIC sysy_ir_core)

# ================================================================
# IR 核心单元测试（不依赖 ANTLR 输入，但需 ANTLR 运行时符号）
# ================================================================
add_executable(test_ir
    tests/test_ir.cppx
    ${ANTLR_SOURCES}
)
set_source_files_properties(tests/test_ir.cppx PROPERTIES LANGUAGE CXX)
target_include_directories(test_ir PRIVATE ${ANTLR_INCLUDES})
target_link_libraries(test_ir sysy_ir_builder sysy_opt ${ANTLR4_RUNTIME_TARGET})

# Focused register-allocation regressions. The source is intentionally optional:
# official packaging omits standalone test mains, while local developers may
# restore the guarded fixture when working on the allocator.
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_register_allocator.cpp")
    add_executable(test_register_allocator
        tests/test_register_allocator.cpp
    )
    target_include_directories(test_register_allocator PRIVATE include)
    target_compile_definitions(test_register_allocator PRIVATE LOCAL_REGISTER_ALLOCATOR_TEST)
    target_link_libraries(test_register_allocator PRIVATE sysy_backend sysy_opt)
endif()

# Focused backend peephole regression tests. Keep these independent from the
# IR pipeline fixtures so a target-code safety fix can be validated in
# isolation.
add_executable(test_peephole
    tests/test_peephole.cppx
)
set_source_files_properties(tests/test_peephole.cppx PROPERTIES LANGUAGE CXX)
target_include_directories(test_peephole PRIVATE include)
target_link_libraries(test_peephole PRIVATE sysy_opt)

# ================================================================
# 编译器主入口 compiler
# ================================================================
add_executable(compiler
    src/main.cpp
    ${ANTLR_SOURCES}
)
target_include_directories(compiler PRIVATE include ${ANTLR_INCLUDES})
target_link_libraries(compiler sysy_compiler sysy_backend sysy_opt ${ANTLR4_RUNTIME_TARGET})

# ================================================================
# 集成测试
# ================================================================
add_executable(test_integration
    tests/test_integration.cppx
    ${ANTLR_SOURCES}
)
set_source_files_properties(tests/test_integration.cppx PROPERTIES LANGUAGE CXX)
target_include_directories(test_integration PRIVATE include ${ANTLR_INCLUDES})
target_link_libraries(test_integration sysy_compiler sysy_opt ${ANTLR4_RUNTIME_TARGET})

if(BUILD_TESTING)
    add_test(NAME ir_unit COMMAND test_ir)
    add_test(NAME compiler_integration COMMAND test_integration)
    add_test(NAME peephole_unit COMMAND test_peephole)
    set_tests_properties(ir_unit compiler_integration peephole_unit PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    if(TARGET test_register_allocator)
        add_test(NAME register_allocator_unit COMMAND test_register_allocator)
        set_tests_properties(register_allocator_unit PROPERTIES
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif()
endif()
