# 语法测试专用 CMakeLists.txt - 国内网络优化版
cmake_minimum_required(VERSION 3.20)
project(SysY2022GrammarTest VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===== 查找 ANTLR4 =====
# 方式1: 系统安装的 antlr4
# 方式2: 下载预编译的 antlr4-runtime

find_path(ANTLR4_INCLUDE_DIR
    NAMES antlr4-runtime.h
    PATHS /usr/include/antlr4-runtime
          /usr/local/include/antlr4-runtime
          /opt/antlr4/include)

find_library(ANTLR4_LIBRARY
    NAMES antlr4-runtime antlr4-runtime-static
    PATHS /usr/lib
          /usr/local/lib
          /opt/antlr4/lib)

if(NOT ANTLR4_INCLUDE_DIR OR NOT ANTLR4_LIBRARY)
    message(STATUS "未找到系统安装的 ANTLR4，使用预编译包...")

    # 使用 GitHub 镜像下载预编译的 ANTLR4 C++ Runtime
    include(FetchContent)
    
    # 配置 GitHub 镜像
    set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE ALWAYS)
    
    FetchContent_Declare(
        antlr4
        URL https://github.com/antlr/antlr4/archive/refs/tags/4.13.1.tar.gz
        URL_HASH SHA256=131655e142064948e0d49181354b4998d99f05762b545fa04119caa4c3da8ab8
        DOWNLOAD_NO_EXTRACT FALSE
    )
    
    # 尝试使用国内镜像（如果直接访问 GitHub 慢）
    # 如果上面的还是慢，可以改为：
    # URL https://ghproxy.com/https://github.com/antlr/antlr4/archive/refs/tags/4.13.1.tar.gz
    
    if(NOT antlr4_POPULATED)
        FetchContent_Populate(antlr4)
        
        set(ANTLR4_SOURCE_DIR ${antlr4_SOURCE_DIR})
        set(ANTLR4_BINARY_DIR ${antlr4_BINARY_DIR})
        
        # 添加子目录，但只构建 C++ Runtime
        add_subdirectory(${ANTLR4_SOURCE_DIR}/runtime/Cpp ${ANTLR4_BINARY_DIR} EXCLUDE_FROM_ALL)
        
        set(ANTLR4_INCLUDE_DIR
            ${ANTLR4_SOURCE_DIR}/runtime/Cpp/runtime/src
            ${ANTLR4_BINARY_DIR}
        )
        set(ANTLR4_LIBRARY antlr4_static)
        set(ANTLR4_DEPENDENCY antlr4_static)
    endif()
else()
    message(STATUS "使用系统安装的 ANTLR4")
endif()

# ===== 生成 ANTLR4 分析器 =====
find_program(ANTLR4_JAR
    NAMES antlr-4.13.1-complete.jar antlr4.jar
    PATHS /usr/local/lib /opt/antlr4
    DOC "ANTLR4 JAR 文件")

if(NOT ANTLR4_JAR)
    message(STATUS "未找到 ANTLR4 JAR，尝试使用 antlr4 命令")
    set(ANTLR4_COMMAND "antlr4")
else()
    set(ANTLR4_COMMAND "java" "-jar" "${ANTLR4_JAR}")
endif()

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(LEXER_G4 ${CMAKE_SOURCE_DIR}/grammar/SysY2022Lexer.g4)
set(PARSER_G4 ${CMAKE_SOURCE_DIR}/grammar/SysY2022Parser.g4)

set(GENERATED_SOURCES
    ${GENERATED_DIR}/SysY2022Lexer.cpp
    ${GENERATED_DIR}/SysY2022Parser.cpp
    ${GENERATED_DIR}/SysY2022Lexer.h
    ${GENERATED_DIR}/SysY2022Parser.h
    ${GENERATED_DIR}/SysY2022ParserBaseListener.cpp
    ${GENERATED_DIR}/SysY2022ParserBaseListener.h
    ${GENERATED_DIR}/SysY2022ParserListener.cpp
    ${GENERATED_DIR}/SysY2022ParserListener.h
    ${GENERATED_DIR}/SysY2022ParserBaseVisitor.cpp
    ${GENERATED_DIR}/SysY2022ParserBaseVisitor.h
    ${GENERATED_DIR}/SysY2022ParserVisitor.cpp
    ${GENERATED_DIR}/SysY2022ParserVisitor.h
)

add_custom_command(
    OUTPUT ${GENERATED_SOURCES}
    COMMAND ${ANTLR4_COMMAND} -Dlanguage=Cpp -o ${GENERATED_DIR} ${LEXER_G4}
    COMMAND ${ANTLR4_COMMAND} -Dlanguage=Cpp -o ${GENERATED_DIR} ${PARSER_G4}
    DEPENDS ${LEXER_G4} ${PARSER_G4}
    COMMENT "生成 ANTLR4 分析器"
)

# ===== 编译测试程序 =====
add_executable(test-grammar
    ${CMAKE_SOURCE_DIR}/src/test-grammar.cpp
    ${GENERATED_SOURCES}
)

target_include_directories(test-grammar
    PRIVATE
    ${GENERATED_DIR}
    ${ANTLR4_INCLUDE_DIR}
)

target_link_libraries(test-grammar
    ${ANTLR4_LIBRARY}
)

if(ANTLR4_DEPENDENCY)
    add_dependencies(test-grammar ${ANTLR4_DEPENDENCY})
endif()

message(STATUS "==================================")
message(STATUS "配置完成!")
message(STATUS "构建命令:")
message(STATUS "  make test-grammar")
message(STATUS "运行测试:")
message(STATUS "  ./test-grammar ../test/hello.sy")
message(STATUS "==================================")
