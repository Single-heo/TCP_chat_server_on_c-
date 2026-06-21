#pragma once

#include <fstream>
#include <string>
#include "common/config/Configuration.hpp"

class Logger {
public:
    explicit Logger(const ServerConfig& config);
    enum LogType {
        Info,
        Warn,
        Error
    };
    
    bool IsLoggingEnabled() const;
    void Write_log(const std::string& message, LogType type);
    std::string getPath() const; // Marked const
    static std::string getTime();
    ~Logger();

private:
    std::ofstream LogFile;
    std::string logPath;
    bool runWithoutLogging{false};
};