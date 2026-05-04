#pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
private:
    LogLevel minLevel;
    static Logger* instance;

    Logger() : minLevel(LogLevel::INFO) {}

public:
    static Logger& getInstance() {
        if (!instance) {
            instance = new Logger();
        }
        return *instance;
    }

    void setLogLevel(LogLevel level) {
        minLevel = level;
    }

    void log(LogLevel level, const std::string& message) {
        if (level < minLevel) return;

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << " ";

        switch (level) {
            case LogLevel::DEBUG: std::cout << "[DEBUG] "; break;
            case LogLevel::INFO: std::cout << "[INFO]  "; break;
            case LogLevel::WARNING: std::cout << "[WARN]  "; break;
            case LogLevel::ERROR: std::cout << "[ERROR] "; break;
        }
        std::cout << message << std::endl;
    }

    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warn(const std::string& msg) { log(LogLevel::WARNING, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
};

Logger* Logger::instance = nullptr;
