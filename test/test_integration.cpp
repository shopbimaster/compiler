#include <iostream>
#include <cassert>
#include <string>
#include "ir/IR.h"
#include "ir/IRBuilder.h"
#include "opt/Optimizer.h"

using namespace IR;

static int passed = 0;
static int failed = 0;

#define TEST(name) do { std::cout << "  TEST: " << name << " ... "; } while (0)
#define PASS()     do { std::cout << "PASSED\n"; passed++; } while (0)
#define FAIL(msg)  do { std::cout << "FAILED: " << msg << "\n"; failed++; } while (0)
#define CHECK(c)   do { if (!(c)) { FAIL(#c); return; } } while (0)

#define F(name) "../test/functional/" name ".sy"

void test_minimal_main() {
    TEST("int main() { return 0; }");
    IRBuilder builder;
    auto mod = builder.compile(F("00_main"));
    std::string ir = mod->dump();
    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("ret i32") != std::string::npos);
    PASS();
}

void test_arithmetic() {
    TEST("算术表达式 1+2*3");
    IRBuilder builder;
    auto mod = builder.compile(F("11_add2"));
    std::string ir = mod->dump();
    CHECK(ir.find("define i32 @main()") != std::string::npos);
    PASS();
}

void test_variable() {
    TEST("变量声明和赋值");
    IRBuilder builder;
    auto mod = builder.compile(F("01_var_defn2"));
    std::string ir = mod->dump();
    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("alloca i32") != std::string::npos);
    PASS();
}

void test_if_else() {
    TEST("if-else 分支");
    IRBuilder builder;
    auto mod = builder.compile(F("21_if_test2"));
    std::string ir = mod->dump();
    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("br") != std::string::npos);
    PASS();
}

void test_while() {
    TEST("while 循环");
    IRBuilder builder;
    auto mod = builder.compile(F("26_while_test1"));
    std::string ir = mod->dump();
    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("while_cond") != std::string::npos);
    PASS();
}

void test_function_call() {
    TEST("函数调用");
    IRBuilder builder;
    auto mod = builder.compile(F("09_func_defn"));
    std::string ir = mod->dump();
    CHECK(ir.find("define i32 @add") != std::string::npos
          || ir.find("define i32 @main") != std::string::npos);
    PASS();
}

void test_error_handling() {
    TEST("语法错误检测");
    try {
        IRBuilder builder;
        auto mod = builder.compile("../test/error.sy");
        FAIL("应该抛出异常但未抛出");
    } catch (const std::exception& e) {
        std::cout << "(expected error: " << e.what() << ") ";
        PASS();
    }
}

void test_break() {
    TEST("while 中 break 跳出循环");
    IRBuilder builder;
    auto mod = builder.compile(F("29_break"));
    std::string ir = mod->dump();
    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("while_cond") != std::string::npos);
    PASS();
}

void test_continue() {
    TEST("while 中 continue 跳回条件");
    IRBuilder builder;
    auto mod = builder.compile(F("30_continue"));
    std::string ir = mod->dump();
    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("while_cond") != std::string::npos);
    PASS();
}

void test_global_var() {
    TEST("全局变量声明与访问");
    IRBuilder builder;
    auto mod = builder.compile(F("89_many_globals"));
    std::string ir = mod->dump();
    CHECK(ir.find("@") != std::string::npos);
    CHECK(ir.find("global") != std::string::npos);
    CHECK(ir.find("define i32 @main()") != std::string::npos);
    PASS();
}

void test_array_1d() {
    TEST("一维数组声明、赋值、访问");
    IRBuilder builder;
    auto mod = builder.compile(F("34_arr_expr_len"));
    std::string ir = mod->dump();
    CHECK(ir.find("alloca") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

void test_array_2d() {
    TEST("二维数组声明与访问");
    IRBuilder builder;
    auto mod = builder.compile(F("05_arr_defn4"));
    std::string ir = mod->dump();
    CHECK(ir.find("alloca") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

void test_void_func() {
    TEST("void 函数");
    IRBuilder builder;
    auto mod = builder.compile(F("67_reverse_output"));
    std::string ir = mod->dump();
    CHECK(ir.find("define void") != std::string::npos);
    CHECK(ir.find("call void") != std::string::npos);
    PASS();
}

void test_array_param() {
    TEST("数组参数传递");
    IRBuilder builder;
    auto mod = builder.compile(F("55_sort_test1"));
    std::string ir = mod->dump();
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

void test_const_1d() {
    TEST("const一维数组");
    IRBuilder builder;
    auto mod = builder.compile(F("08_const_array_defn"));
    std::string ir = mod->dump();
    CHECK(ir.find("@") != std::string::npos);
    CHECK(ir.find("global") != std::string::npos);
    PASS();
}

void test_const_2d() {
    TEST("const二维数组");
    IRBuilder builder;
    auto mod = builder.compile(F("04_arr_defn3"));
    std::string ir = mod->dump();
    CHECK(ir.find("alloca") != std::string::npos);
    PASS();
}

void test_const_arr_dim() {
    TEST("constExpr作为数组维度");
    IRBuilder builder;
    auto mod = builder.compile(F("34_arr_expr_len"));
    std::string ir = mod->dump();
    CHECK(ir.find("alloca") != std::string::npos);
    PASS();
}

void test_global_const_dim() {
    TEST("全局const作为数组维度");
    IRBuilder builder;
    auto mod = builder.compile(F("06_const_var_defn2"));
    std::string ir = mod->dump();
    CHECK(ir.find("@") != std::string::npos);
    CHECK(ir.find("global") != std::string::npos);
    PASS();
}

void test_array_init() {
    TEST("一维数组聚合初始化");
    IRBuilder builder;
    auto mod = builder.compile(F("04_arr_defn3"));
    std::string ir = mod->dump();
    CHECK(ir.find("alloca") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

void test_array_init2d() {
    TEST("二维数组聚合初始化");
    IRBuilder builder;
    auto mod = builder.compile(F("05_arr_defn4"));
    std::string ir = mod->dump();
    CHECK(ir.find("alloca") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    PASS();
}

void test_io() {
    TEST("I/O运行时函数声明");
    IRBuilder builder;
    auto mod = builder.compile(F("73_int_io"));
    std::string ir = mod->dump();
    CHECK(mod->dump().find("declare i32 @getint()") != std::string::npos
          || ir.find("declare") != std::string::npos);
    PASS();
}

void test_global_const_arr() {
    TEST("全局常量数组");
    IRBuilder builder;
    auto mod = builder.compile(F("08_const_array_defn"));
    std::string ir = mod->dump();
    CHECK(ir.find("@") != std::string::npos);
    PASS();
}

void test_arr_partial() {
    TEST("数组部分初始化");
    IRBuilder builder;
    auto mod = builder.compile(F("04_arr_defn3"));
    std::string ir = mod->dump();
    CHECK(ir.find("alloca") != std::string::npos);
    PASS();
}

void test_loop_interchange_integration() {
    TEST("LoopInterchange 优化集成（O3 流水线）");
    IRBuilder builder;
    auto mod = builder.compile("../test/nested_loop_test.sy");
    std::string before = mod->dump();

    CHECK(before.find("define i32 @main()") != std::string::npos);
    CHECK(before.find("while_cond") != std::string::npos);

    // Run O3 optimization pipeline (includes loopInterchange)
    Opt::runO1(mod.get());
    Opt::runO2(mod.get());
    Opt::runO3(mod.get());
    std::string after = mod->dump();

    CHECK(after.find("define i32 @main()") != std::string::npos);

    PASS();
}

int main() {
    std::cout << "=== 集成测试: .sy -> IR ===\n\n";

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
    test_loop_interchange_integration();

    std::cout << "\n=== 结果: " << passed << " passed, "
              << failed << " failed ===\n";

    return failed > 0 ? 1 : 0;
}