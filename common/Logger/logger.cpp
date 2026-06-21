#include "logger.hpp"
#include <iomanip>
#include <sstream>
#include <ctime>
#include <stdexcept>
#include <unistd.h> // Using standard POSIX sleep for single-threaded lightness

Logger::Logger(const ServerConfig& config)
    : logPath(config.LogPath),
      runWithoutLogging(config.Run_without_logging)
{
    if (runWithoutLogging)
        return;

    for (int i = 0; i < 5; ++i) {
        LogFile.open(logPath, std::ios::app);
        if (LogFile.is_open())
            break;
            
        sleep(1); // Standard POSIX sleep—completely safe for single-thread without C++ std::thread overhead
    }

    if (!LogFile.is_open()) {
        throw std::runtime_error("Could not open log file: " + logPath);
    }
}

std::string Logger::getTime() {
    std::time_t now = std::time(nullptr);
    std::tm local_time;
    
    // Thread-safe and secure POSIX alternative to std::localtime
    localtime_r(&now, &local_time); 

    std::stringstream ss;
    ss << std::put_time(&local_time, "%H:%M:%S");
    return ss.str();
}

Logger::~Logger() {
    if (LogFile.is_open()) {
        LogFile.close();
    }
}

bool Logger::IsLoggingEnabled() const {
    return !runWithoutLogging && LogFile.is_open();
}

void Logger::Write_log(const std::string& message, LogType type) {
    if (!IsLoggingEnabled())
        return;

    std::string prefix{};
    switch (type) {
        case Info:  prefix = "[INFO]"; break;
        case Warn:  prefix = "[WARN]"; break;
        case Error: prefix = "[ERROR]"; break;
    }

    // Cleaned up raw file streaming without breaking ANSI escape text layouts
    LogFile << getTime() << " " << prefix << ": " << message << std::endl; 
}

std::string Logger::getPath() const {
    return logPath;
}