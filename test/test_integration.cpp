#include <iostream>
#include <cassert>
#include <string>
#include "ir/IR.h"
#include "ir/IRBuilder.h"
#include "ir/IR.h"

using namespace IR;

static int passed = 0;
static int failed = 0;

#define TEST(name) do { std::cout << "  TEST: " << name << " ... "; } while (0)
#define PASS()     do { std::cout << "PASSED\n"; passed++; } while (0)
#define FAIL(msg)  do { std::cout << "FAILED: " << msg << "\n"; failed++; } while (0)
#define CHECK(c)   do { if (!(c)) { FAIL(#c); return; } } while (0)

// ================================================================
// 测试 1: 最小程序 — int main() { return 0; }
// ================================================================
void test_minimal_main() {
    TEST("int main() { return 0; }");
    IRBuilder builder;
    auto mod = builder.compile("../test/hello.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("ret i32 0") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 2: 算术表达式
// ================================================================
void test_arithmetic() {
    TEST("int main() { return 1+2*3; }");
    IRBuilder builder;
    auto mod = builder.compile("../test/arithmetic.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define i32 @main()") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 3: 变量声明和赋值
// ================================================================
void test_variable() {
    TEST("int main() { int a; a = 42; return a; }");
    IRBuilder builder;
    auto mod = builder.compile("../test/variable.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("alloca i32") != std::string::npos);
    CHECK(ir.find("ret i32") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 4: if-else
// ================================================================
void test_if_else() {
    TEST("if-else 分支");
    IRBuilder builder;
    auto mod = builder.compile("../test/ifelse.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("br") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 5: while 循环
// ================================================================
void test_while() {
    TEST("while 循环");
    IRBuilder builder;
    auto mod = builder.compile("../test/while_test.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("while_cond") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 6: 函数调用
// ================================================================
void test_function_call() {
    TEST("函数调用");
    IRBuilder builder;
    auto mod = builder.compile("../test/func_call.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define i32 @add") != std::string::npos
          || ir.find("define i32 @main") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 7: 编译错误检测
// ================================================================
void test_error_handling() {
    TEST("语法错误检测（缺少分号）");
    try {
        IRBuilder builder;
        auto mod = builder.compile("../test/bad.sy");
        FAIL("应该抛出异常但未抛出");
    } catch (const std::exception& e) {
        std::cout << "(expected error: " << e.what() << ") ";
        PASS();
    }
}

// ================================================================
int main() {
    std::cout << "=== 集成测试: .sy → IR ===\n\n";

    test_minimal_main();
    test_arithmetic();
    test_variable();
    test_if_else();
    test_while();
    test_function_call();
    test_error_handling();

    std::cout << "\n=== 结果: " << passed << " passed, "
              << failed << " failed ===\n";

    return failed > 0 ? 1 : 0;
}