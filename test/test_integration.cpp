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
// 测试 8: break 在 while 循环中
// ================================================================
void test_break() {
    TEST("while 中 break 跳出循环");
    IRBuilder builder;
    auto mod = builder.compile("../test/break_test.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("while_cond") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 9: continue 在 while 循环中
// ================================================================
void test_continue() {
    TEST("while 中 continue 跳回条件");
    IRBuilder builder;
    auto mod = builder.compile("../test/continue_test.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("while_cond") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 10: 全局变量
// ================================================================
void test_global_var() {
    TEST("全局变量声明与访问");
    IRBuilder builder;
    auto mod = builder.compile("../test/global_var.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("@g = global") != std::string::npos);
    CHECK(ir.find("define i32 @main()") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 11: 一维数组
// ================================================================
void test_array_1d() {
    TEST("一维数组声明、赋值、访问");
    IRBuilder builder;
    auto mod = builder.compile("../test/array_1d.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("alloca [3 x i32]") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 12: 二维数组
// ================================================================
void test_array_2d() {
    TEST("二维数组声明与访问");
    IRBuilder builder;
    auto mod = builder.compile("../test/array_2d.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("alloca [2 x [3 x i32]]") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 13: void 函数
// ================================================================
void test_void_func() {
    TEST("void 函数 — 无返回值");
    IRBuilder builder;
    auto mod = builder.compile("../test/void_func.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define void @setResult") != std::string::npos);
    CHECK(ir.find("call void") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 14: 数组参数
// ================================================================
void test_array_param() {
    TEST("数组参数传递");
    IRBuilder builder;
    auto mod = builder.compile("../test/array_param.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("define i32 @sum") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 15: 常量一维数组初始化
// ================================================================
void test_const_1d() {
    TEST("const一维数组初始化");
    IRBuilder builder;
    auto mod = builder.compile("../test/const_1d.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("alloca [2 x i32]") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    CHECK(ir.find("store i32 1") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 16: 常量二维数组初始化
// ================================================================
void test_const_2d() {
    TEST("const二维数组初始化");
    IRBuilder builder;
    auto mod = builder.compile("../test/const_2d.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("alloca [2 x [3 x i32]]") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 17: const表达式作为数组维度
// ================================================================
void test_const_arr_dim() {
    TEST("constExpr作为数组维度");
    IRBuilder builder;
    auto mod = builder.compile("../test/const_arr_dim.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("alloca [4 x [2 x i32]]") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 18: 全局const作为数组维度
// ================================================================
void test_global_const_dim() {
    TEST("全局const作为数组维度");
    IRBuilder builder;
    auto mod = builder.compile("../test/global_const_dim.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("@a = global [10 x i32]") != std::string::npos);
    CHECK(ir.find("@N = global i32") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 19: 一维数组聚合初始化
// ================================================================
void test_array_init() {
    TEST("一维数组聚合初始化");
    IRBuilder builder;
    auto mod = builder.compile("../test/array_init.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("alloca [3 x i32]") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    CHECK(ir.find("store i32 1") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 20: 二维数组聚合初始化
// ================================================================
void test_array_init2d() {
    TEST("二维数组聚合初始化");
    IRBuilder builder;
    auto mod = builder.compile("../test/array_init2d.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("alloca [2 x [3 x i32]]") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 21: I/O 运行时函数声明
// ================================================================
void test_io() {
    TEST("I/O运行时函数声明与调用");
    IRBuilder builder;
    auto mod = builder.compile("../test/io_test.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("declare i32 @getint()") != std::string::npos);
    CHECK(ir.find("declare void @putint(i32") != std::string::npos);
    CHECK(ir.find("declare void @putch(i32") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 22: 全局常量数组
// ================================================================
void test_global_const_arr() {
    TEST("全局常量数组");
    IRBuilder builder;
    auto mod = builder.compile("../test/global_const_arr.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("@a = global [4 x [2 x i32]]") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 23: 数组部分初始化
// ================================================================
void test_arr_partial() {
    TEST("数组部分初始化（缺失元素补零）");
    IRBuilder builder;
    auto mod = builder.compile("../test/arr_partial.sy");
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n" << ir
              << "-------------------------------\n";

    CHECK(ir.find("alloca [4 x [2 x i32]]") != std::string::npos);
    PASS();
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
    test_break();
    test_continue();
    test_global_var();
    test_array_1d();
    test_array_2d();
    test_void_func();
    test_array_param();
    test_const_1d();
    test_const_2d();
    test_const_arr_dim();
    test_global_const_dim();
    test_array_init();
    test_array_init2d();
    test_io();
    test_global_const_arr();
    test_arr_partial();

    std::cout << "\n=== 结果: " << passed << " passed, "
              << failed << " failed ===\n";

    return failed > 0 ? 1 : 0;
}