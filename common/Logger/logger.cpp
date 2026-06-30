#include "logger.hpp"
#include <iomanip>
#include <sstream>
#include <ctime>
#include <stdexcept>
#include <iostream>
#include <syslog.h>
#include <unistd.h>

Logger::Logger(const ServerConfig& config)
    : logPath(config.LogPath),
      runWithoutLogging(config.Run_without_logging)
{
    // logging-off → não abre arquivo, mas journald segue ativo
    if (runWithoutLogging) {
        std::cout << "<" << LOG_INFO << ">[INFO]: file logging disabled, "
                     "journald only\n";
        std::cout.flush();
        return;
    }

    for (int i = 0; i < 5; ++i) {
        LogFile.open(logPath, std::ios::app);
        if (LogFile.is_open())
            break;
        sleep(1);
    }

    // Arquivo falhou: NÃO aborta — degrada para journald-only
    if (!LogFile.is_open()) {
        std::cout << "<" << LOG_WARNING << ">[WARN]: could not open log file: "
                  << logPath << " — continuing with journald only\n";
        std::cout.flush();
    }
}

std::string Logger::getTime() {
    std::time_t now = std::time(nullptr);
    std::tm local_time;
    localtime_r(&now, &local_time);

    std::stringstream ss;
    ss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S%z"); // ISO-8601 + offset
    return ss.str();
}

Logger::~Logger() {
    if (LogFile.is_open())
        LogFile.close();
}

// journald: ativo sempre que NÃO estiver em modo "sem logging total"
// (aqui mantemos journald sempre on, independente da flag de arquivo)
bool Logger::IsFileLoggingActive() const {
    return LogFile.is_open();   // reflete só o estado do arquivo
}
int Logger::syslogPriority(LogType type) const {
    switch (type) {
        case Info:  return LOG_INFO;
        case Warn:  return LOG_WARNING;
        case Error: return LOG_ERR;
    }
    return LOG_INFO;
}

const char* Logger::prefix(LogType type) const {
    switch (type) {
        case Info:  return "[INFO]";
        case Warn:  return "[WARN]";
        case Error: return "[ERROR]";
    }
    return "[INFO]";
}

void Logger::Write_log(const std::string& message, LogType type) {
    // 1) journald — sempre
    std::cout << "<" << syslogPriority(type) << ">"
              << prefix(type) << ": " << message << "\n";
    std::cout.flush();

    // 2) arquivo — só se aberto
    if (IsFileLoggingActive()) {
        LogFile << getTime() << " " << prefix(type) << ": "
                << message << std::endl;
    }
}

std::string Logger::getPath() const {
    return logPath;
}
