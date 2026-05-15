#include <iostream>
#include <fstream>
#include <string>
#include "antlr4-runtime.h"
#include "SysY2022Lexer.h"
#include "SysY2022Parser.h"

using namespace antlr4;

int main(int argc, const char* argv[]) {
    std::cout << "==========================================" << std::endl;
    std::cout << "SysY2022 语法测试程序" << std::endl;
    std::cout << "==========================================" << std::endl;

    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <输入文件.sy>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    std::cout << "\n正在解析: " << filename << std::endl;

    try {
        // 1. 读取文件
        std::ifstream stream;
        stream.open(filename);
        if (!stream.is_open()) {
            std::cerr << "错误: 无法打开文件 " << filename << std::endl;
            return 1;
        }

        // 2. 词法分析
        ANTLRInputStream input(stream);
        SysY2022Lexer lexer(&input);
        CommonTokenStream tokens(&lexer);

        // 3. 语法分析
        SysY2022Parser parser(&tokens);

        // 添加错误监听器
        class TestErrorListener : public BaseErrorListener {
        public:
            bool hasError = false;

            void syntaxError(Recognizer* recognizer,
                             Token* offendingSymbol,
                             size_t line,
                             size_t charPositionInLine,
                             const std::string& msg,
                             std::exception_ptr e) override {
                hasError = true;
                std::cerr << "语法错误 [" << line << ":" << charPositionInLine << "] "
                          << msg << std::endl;
            }
        };

        TestErrorListener errorListener;
        parser.removeErrorListeners();
        parser.addErrorListener(&errorListener);

        // 4. 解析
        tree::ParseTree* tree = parser.compilationUnit();

        // 5. 检查结果
        if (errorListener.hasError) {
            std::cout << "\n❌ 解析失败" << std::endl;
            return 1;
        }

        std::cout << "\n✅ 解析成功!" << std::endl;
        std::cout << "ParseTree: " << tree->toStringTree(&parser) << std::endl;

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }
}
