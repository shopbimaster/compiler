#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <sstream>

enum class ErrorType {
    LEXICAL,
    SYNTAX,
    SEMANTIC,
    CODEGEN,
    INTERNAL
};

struct Error {
    ErrorType type;
    std::string message;
    std::string fileName;
    int line;
    int column;

    Error(ErrorType t, const std::string& msg, const std::string& file, int l, int c)
        : type(t), message(msg), fileName(file), line(l), column(c) {}

    std::string toString() const {
        std::string typeStr;
        switch (type) {
            case ErrorType::LEXICAL: typeStr = "Lexical"; break;
            case ErrorType::SYNTAX: typeStr = "Syntax"; break;
            case ErrorType::SEMANTIC: typeStr = "Semantic"; break;
            case ErrorType::CODEGEN: typeStr = "CodeGen"; break;
            case ErrorType::INTERNAL: typeStr = "Internal"; break;
        }
        std::ostringstream oss;
        oss << fileName << ":" << line << ":" << column << ": "
            << typeStr << " error: " << message;
        return oss.str();
    }
};

class ErrorReporter {
private:
    std::vector<Error> errors;
    std::string fileName;
    bool hasErrorFlag;

public:
    explicit ErrorReporter(const std::string& file) : fileName(file), hasErrorFlag(false) {}

    void report(ErrorType type, const std::string& message, int line, int column) {
        errors.emplace_back(type, message, fileName, line, column);
        hasErrorFlag = true;
    }

    bool hasError() const {
        return hasErrorFlag;
    }

    const std::vector<Error>& getErrors() const {
        return errors;
    }

    void printErrors() const {
        for (const auto& err : errors) {
            std::cerr << err.toString() << std::endl;
        }
    }

    void clear() {
        errors.clear();
        hasErrorFlag = false;
    }
};
