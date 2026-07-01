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

// Global atomic pointer so the signal handler can safely reach the server
// instance without relying on globals with non-trivial construction/destruction
// order, or on locking (which isn't signal-safe).
std::atomic<TcpServer*> g_server_instance{nullptr};

// Async-signal-safe handler: ONLY publishes the intent to stop.
// No maps, no close(), no logging — just an atomic store inside requestShutdown().
// Anything more (I/O, allocation, mutexes) would be undefined behavior inside
// a signal handler.
void handle_shutdown_signal(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        TcpServer* server = g_server_instance.load();
        if (server) {
            server->requestShutdown(); // only does SERVER_IS_RUNNING.store(false)
        }
    }
}

// Forward declaration — defined below main.
// Checks whether `ip` is bound to any local network interface on this host.
bool isLocalIP(const std::string& ip);

int main()
{
    // Load runtime settings (address, port, paths, etc.) from the .ini file.
    ServerConfig config;
    if (!config.Load("/etc/tcpserver/Config_file.ini")) {
        std::cerr << "Failed to load configuration file." << std::endl;
        return EXIT_FAILURE;
    }

    // Logger is created early so even config/address validation errors
    // get recorded (journald + optional file sink).
    Logger logger(config);

    if (isLocalIP(config.address))
    {
        // Server owns its own lifetime via unique_ptr; raw pointer is only
        // exposed to the signal handler through the atomic global.
        std::unique_ptr<TcpServer> server =
            std::make_unique<TcpServer>(
                config.port,
                config.address.c_str(),
                &logger
            );

        // Publish pointer BEFORE registering handlers, so a signal arriving
        // right after std::signal() can never see a null/stale pointer.
        g_server_instance.store(server.get());

        std::signal(SIGTERM, handle_shutdown_signal); // systemd stop
        std::signal(SIGINT,  handle_shutdown_signal); // Ctrl+C
        std::signal(SIGPIPE, SIG_IGN);                // ignore broken pipe (avoid default terminate on write to closed socket)

        logger.Write_log("Server started on " + config.address + ":" + std::to_string(config.port), Logger::Info);

        // Blocks until the atomic flag flips (via signal or internal logic);
        // all teardown (epoll, fds, clients) now happens inside run().
        server->run();

        // Unpublish before the unique_ptr destroys the instance, so a signal
        // arriving during destruction can never dereference a dangling pointer.
        g_server_instance.store(nullptr);

        logger.Write_log("Server stopped gracefully.", Logger::Info);
        return 0;
    }
    else
    {
        // Fail fast: refuse to start if the configured address isn't actually
        // reachable on this host (avoids silent bind failures later).
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
// Iterates all network interfaces via getifaddrs() and compares IPv4
// addresses only (IPv6 interfaces are skipped).
// ---------------------------------------------------------------------------
bool isLocalIP(const std::string& ip)
{
    struct ifaddrs *ifaddr, *ifa;

    // Populates a linked list of all local interfaces (IPv4, IPv6, etc.).
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return false;
    }

    bool found = false;

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr) continue; // Some interfaces have a null address entry

        if (ifa->ifa_addr->sa_family == AF_INET) // Only compare IPv4 addresses
        {
            char addr[INET_ADDRSTRLEN];
            void* in_addr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, in_addr, addr, INET_ADDRSTRLEN); // Binary → dotted-decimal string

            if (ip == addr) {
                found = true;
                break;
            }
        }
    }

    freeifaddrs(ifaddr); // Always free the list, regardless of match result
    return found;
}
