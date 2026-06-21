#pragma once

#include <string>
#define SI_NO_CONVERSION
#include <simpleini/SimpleIni.h>

struct ServerConfig {
    std::string address; 
    std::string LogPath;
    std::string DatabasePath;
    int port;
    int maxConnections;
    int timeout;
    bool Run_without_logging{false};

    bool Load(const char* file) {
        CSimpleIniA ini;

        if (ini.LoadFile(file) < 0)
            return false;

        address =
            ini.GetValue("NETWORK", "listen_address", "0.0.0.0");

        port =
            (int)ini.GetLongValue("NETWORK", "listen_port", 25565);

        maxConnections =
            (int)ini.GetLongValue("NETWORK", "max_connections", 100);

        timeout =
            (int)ini.GetLongValue("NETWORK", "connection_timeout", 150);

        LogPath = 
            ini.GetValue("LOGS", "LogPath", "/var/log/tcpserver/log.txt" );

        DatabasePath = 
            ini.GetValue("DATABASE", "DatabasePath", "/var/lib/tcpserver/credentials.json");

        Run_without_logging = 
            (bool)ini.GetBoolValue("LOGS", "Run_Without_loggin", false);

        return true;
    }
};
