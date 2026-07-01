#pragma once

#include <fstream>
#include <string>
#include "common/config/Configuration.hpp" // ServerConfig (LogPath, Run_without_logging)

// Logger — dual-sink logger: always writes to stdout (captured by
// journald/systemd via the "<priority>" prefix convention), and optionally
// mirrors to a log file. Degrades gracefully if the file sink fails to open.
class Logger {
public:
    // Builds the logger from config; opens the log file unless logging is off.
    explicit Logger(const ServerConfig& config);

    // Severity levels, mapped to syslog priorities internally.
    enum LogType {
        Info,
        Warn,
        Error
    };

    // True only if the log FILE sink is currently open (journald is independent).
    bool IsFileLoggingActive() const;   // renamed from: IsLoggingEnabled

    // Writes one entry to journald (always) and to the file (if open).
    void Write_log(const std::string& message, LogType type);

    // Returns the configured log file path.
    std::string getPath() const;

    // Current local time as ISO-8601 with UTC offset. Static (no instance needed).
    static std::string getTime();

    ~Logger();

private:
    // Maps a LogType to its LOG_* syslog priority (for the journald prefix).
    int  syslogPriority(LogType type) const;

    // Maps a LogType to a human-readable text tag, e.g. "[WARN]".
    const char* prefix(LogType type) const;

    std::ofstream LogFile;            // File sink (may stay closed)
    std::string logPath;              // Target path from config
    bool runWithoutLogging{false};    // true → skip file sink entirely
};
