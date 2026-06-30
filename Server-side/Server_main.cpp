#include <csignal>       // std::signal
#include <cstdlib>       // EXIT_FAILURE
#include <iostream>      // std::cout, std::cerr
#include <ifaddrs.h>     // getifaddrs(), freeifaddrs()
#include <arpa/inet.h>   // inet_ntop()
#include <cstring>       // memset(), strerror()
#include <atomic>        // std::atomic
#include <memory>        // std::unique_ptr
#include "common/Logger/logger.hpp"
#include "server-header.hpp"

// Global atomic pointer so the signal handler can safely reach the server.
std::atomic<TcpServer*> g_server_instance{nullptr};

// Async-signal-safe handler: ONLY publishes the intent to stop.
// No maps, no close(), no logging — just an atomic store inside requestShutdown().
void handle_shutdown_signal(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        TcpServer* server = g_server_instance.load();
        if (server) {
            server->requestShutdown(); // only does SERVER_IS_RUNNING.store(false)
        }
    }
}

// Forward declaration — defined below main.
bool isLocalIP(const std::string& ip);

int main()
{
    ServerConfig config;
    if (!config.Load("/etc/tcpserver/Config_file.ini")) {
        std::cerr << "Failed to load configuration file." << std::endl;
        return EXIT_FAILURE;
    }
    Logger logger(config);

    if (isLocalIP(config.address))
    {
        std::unique_ptr<TcpServer> server =
            std::make_unique<TcpServer>(
                config.port,
                config.address.c_str(),
                &logger
            );

        // Publish pointer BEFORE registering handlers.
        g_server_instance.store(server.get());

        std::signal(SIGTERM, handle_shutdown_signal); // systemd stop
        std::signal(SIGINT,  handle_shutdown_signal); // Ctrl+C
        std::signal(SIGPIPE, SIG_IGN);                // ignore broken pipe

        logger.Write_log("Server started on " + config.address + ":" + std::to_string(config.port), Logger::Info);

        // Blocks until the flag flips; teardown now happens inside run().
        server->run();

        // Unpublish before the unique_ptr destroys the instance.
        g_server_instance.store(nullptr);

        logger.Write_log("Server stopped gracefully.", Logger::Info);
        return 0;
    }
    else
    {
        std::cerr << BOLD << RED << "[ERROR]:" << NC
                  << " The specified listen address (" << config.address
                  << ") is not assigned to any local network interface on this machine."
                     " Please use a valid local IP address." << std::endl;
        logger.Write_log("Invalid listen address: " + config.address, Logger::Error);
        return EXIT_FAILURE;
    }
}

// ---------------------------------------------------------------------------
// isLocalIP: checks whether the given IP is assigned to a local interface.
// ---------------------------------------------------------------------------
bool isLocalIP(const std::string& ip)
{
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return false;
    }

    bool found = false;

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr) continue;

        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            char addr[INET_ADDRSTRLEN];
            void* in_addr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, in_addr, addr, INET_ADDRSTRLEN);

            if (ip == addr) {
                found = true;
                break;
            }
        }
    }

    freeifaddrs(ifaddr);
    return found;
}
