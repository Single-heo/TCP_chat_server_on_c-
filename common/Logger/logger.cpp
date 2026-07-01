#include "logger.hpp"
#include <iomanip>
#include <sstream>
#include <ctime>
#include <stdexcept>
#include <iostream>
#include <syslog.h>
#include <unistd.h>

// ============================================================================
// Constructor — copies config values, then tries to open the file sink
// unless file logging was explicitly disabled.
// ============================================================================
Logger::Logger(const ServerConfig& config)
    : logPath(config.LogPath),
      runWithoutLogging(config.Run_without_logging)
{
    // Logging off → doesn't open the file, but journald (stdout) stays active.
    if (runWithoutLogging) {
        std::cout << "<" << LOG_INFO << ">[INFO]: file logging disabled, "
                     "journald only\n";
        std::cout.flush(); // Ensure the message reaches journald immediately
        return;
    }

    // Retry opening the file up to 5 times (handles transient issues like
    // the target directory not being mounted/ready yet at boot).
    for (int i = 0; i < 5; ++i) {
        LogFile.open(logPath, std::ios::app); // Append mode — never truncate existing logs
        if (LogFile.is_open())
            break;
        sleep(1); // Wait 1s before retrying
    }

    // File failed after all retries: does NOT abort — degrades to journald-only.
    if (!LogFile.is_open()) {
        std::cout << "<" << LOG_WARNING << ">[WARN]: could not open log file: "
                  << logPath << " — continuing with journald only\n";
        std::cout.flush();
    }
}

// Formats the current local time as "YYYY-MM-DD HH:MM:SS+HHMM" (ISO-8601-like
// with UTC offset). Uses localtime_r for thread-safety (vs. localtime()).
std::string Logger::getTime() {
    std::time_t now = std::time(nullptr);
    std::tm local_time;
    localtime_r(&now, &local_time);

    std::stringstream ss;
    ss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S%z"); // ISO-8601 + offset
    return ss.str();
}

// Closes the file sink cleanly, if it was open.
Logger::~Logger() {
    if (LogFile.is_open())
        LogFile.close();
}

// journald (stdout) sink is considered always-on and independent of this flag;
// this only reflects whether the file sink is currently open.
bool Logger::IsFileLoggingActive() const {
    return LogFile.is_open();
}

// Translates our internal LogType into the matching syslog priority constant,
// used as the "<N>" prefix that journald/systemd parses to assign severity.
int Logger::syslogPriority(LogType type) const {
    switch (type) {
        case Info:  return LOG_INFO;
        case Warn:  return LOG_WARNING;
        case Error: return LOG_ERR;
    }
    return LOG_INFO; // Fallback for unexpected enum values
}

// Human-readable tag used in both the stdout line and the file line.
const char* Logger::prefix(LogType type) const {
    switch (type) {
        case Info:  return "[INFO]";
        case Warn:  return "[WARN]";
        case Error: return "[ERROR]";
    }
    return "[INFO]"; // Fallback for unexpected enum values
}

// Writes a single log entry to both sinks:
//   1) stdout, prefixed with "<priority>" so journald assigns correct severity
//      — always executed, regardless of file sink state.
//   2) the log file, only if it's currently open — includes a timestamp since
//      journald already timestamps entries on its own.
void Logger::Write_log(const std::string& message, LogType type) {
    // 1) journald — always
    std::cout << "<" << syslogPriority(type) << ">"
              << prefix(type) << ": " << message << "\n";
    std::cout.flush(); // Avoid buffering delays for time-sensitive log lines

    // 2) file — only if open
    if (IsFileLoggingActive()) {
        LogFile << getTime() << " " << prefix(type) << ": "
                << message << std::endl; // std::endl flushes after each entry
    }
}

// Returns the log file path resolved from config at construction time.
std::string Logger::getPath() const {
    return logPath;
}
