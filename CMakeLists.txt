cmake_minimum_required(VERSION 3.20)
project(SysY2022Compiler VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ================================================================
# IR 模块库（无 ANTLR4 依赖，可独立编译和测试）
# ================================================================
add_library(sysy_ir STATIC
    src/ir/IR.cpp
    src/ir/IRBuilder.cpp
)

target_include_directories(sysy_ir PUBLIC include)

# ================================================================
# IR 模块单元测试
# ================================================================
add_executable(test_ir
    test/test_ir.cpp
)

target_link_libraries(test_ir sysy_ir)

# ================================================================
# 完整编译器（需要 ANTLR4 —— 待后续集成）
# ================================================================
# add_executable(sysyc
#     src/main.cpp
#     src/Compiler.cpp
#     src/utils/Logger.cpp
#     ${GENERATED_ANTLR_SOURCES}
# )
# target_link_libraries(sysyc sysy_ir)

# ================================================================
# 生成 ANTLR4 前端（待网络可用时启用）
# ================================================================
# find_program(ANTLR4_JAR NAMES antlr-4.13.1-complete.jar antlr4.jar)
# if(NOT ANTLR4_JAR)
#     message(WARNING "ANTLR4 JAR not found. Run setup-ubuntu.sh first.")
# endif()