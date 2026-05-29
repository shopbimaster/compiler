cmake_minimum_required(VERSION 3.20)
project(SysY2022Compiler VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ================================================================
# ANTLR4 C++ Runtime (系统包 libantlr4-runtime-dev)
# ================================================================
find_package(antlr4-runtime REQUIRED)

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
    ${ANTLR4_INCLUDE_DIR}
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
target_link_libraries(sysy_ir_builder PUBLIC sysy_ir_core antlr4_static)

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
)
target_include_directories(sysy_backend PUBLIC include)
target_link_libraries(sysy_backend PUBLIC sysy_ir_core)

# ================================================================
# O1 + O2 + O3 优化库
# ================================================================
add_library(sysy_opt STATIC
    src/opt/Optimizer.cpp
    src/opt/ConstantFolding.cpp
    src/opt/DeadCodeElimination.cpp
    src/opt/PeepholeOptimizer.cpp
    src/opt/InlineExpansion.cpp
    src/opt/CSE.cpp
    src/opt/LICM.cpp
    src/opt/AlgebraicSimplification.cpp
    src/opt/LoopUnrolling.cpp
    src/opt/LoopInterchange.cpp
    src/opt/TailRecursionElimination.cpp
    src/opt/RecursiveMulToNative.cpp
    src/opt/BitOpPatternRecognition.cpp
)
target_include_directories(sysy_opt PUBLIC include)
target_link_libraries(sysy_opt PUBLIC sysy_ir_core)

# ================================================================
# IR 核心单元测试（不依赖 ANTLR 输入，但需 ANTLR 运行时符号）
# ================================================================
add_executable(test_ir
    test/test_ir.cpp
    ${ANTLR_SOURCES}
)
target_include_directories(test_ir PRIVATE ${ANTLR_INCLUDES})
target_link_libraries(test_ir sysy_ir_builder sysy_opt antlr4_static)

# ================================================================
# 编译器主入口 sysyc
# ================================================================
add_executable(sysyc
    src/main.cpp
    ${ANTLR_SOURCES}
)
target_include_directories(sysyc PRIVATE include ${ANTLR_INCLUDES})
target_link_libraries(sysyc sysy_compiler sysy_backend sysy_opt antlr4_static)

# ================================================================
# 集成测试
# ================================================================
add_executable(test_integration
    test/test_integration.cpp
    ${ANTLR_SOURCES}
)
target_include_directories(test_integration PRIVATE include ${ANTLR_INCLUDES})
target_link_libraries(test_integration sysy_compiler sysy_opt antlr4_static)