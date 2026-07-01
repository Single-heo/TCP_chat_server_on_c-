#pragma once

#include <string>
#define SI_NO_CONVERSION                 // Disable SimpleIni Unicode conversion layer
#include <simpleini/SimpleIni.h>

// Holds every runtime setting parsed from the .ini config file.
struct ServerConfig {
    std::string address;       // Bind/listen IP address
    std::string LogPath;       // Destination file for log output
    std::string DatabasePath;  // Path to the credentials JSON store
    std::string PidFilePath;   // PID file path (daemon tracking)
    int port;                  // Listen port
    int maxConnections;        // listen() backlog / global cap
    int timeout;               // Connection idle timeout (seconds)
    bool Run_without_logging{false}; // true → skip file logging (journald only)

    // Parses `file`; returns false if the .ini can't be loaded.
    // Every GetValue call supplies a default, so missing keys are non-fatal.
    bool Load(const char* file) {
        CSimpleIniA ini;

        if (ini.LoadFile(file) < 0)
            return false; // File missing / unreadable

        // ---- [NETWORK] ----
        address =
            ini.GetValue("NETWORK", "listen_address", "127.0.0.1");

        port =
            (int)ini.GetLongValue("NETWORK", "listen_port", 25565);

        maxConnections =
            (int)ini.GetLongValue("NETWORK", "max_connections", 100);

        timeout =
            (int)ini.GetLongValue("NETWORK", "connection_timeout", 150);

        // ---- [LOGS] ----
        LogPath =
            ini.GetValue("LOGS", "LogPath", "/var/log/tcpserver/log.txt");

        // ---- [DATABASE] ----
        DatabasePath =
            ini.GetValue("DATABASE", "DatabasePath", "/var/lib/tcpserver/credentials.json");

        // ---- [PID] ----
        PidFilePath =
            ini.GetValue("PID", "PidFilePath", "/run/tcpserver/server.pid");

        // ---- [LOGS] toggle ----
        Run_without_logging =
            (bool)ini.GetBoolValue("LOGS", "Run_Without_logging", false);

        return true;
    }
};
