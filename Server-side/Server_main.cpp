#include <csignal>
#include <cstdlib>
#include <iostream>
#include "server-header.hpp"
// Global flag to control server loop
volatile sig_atomic_t server_running = 1;

int main() {

    
    int port = 25565;
    TcpServer server(port, "127.0.0.1");
    
    std::cout << "Server starting... Press Ctrl+C to stop.\n";
    server.run();  
    
    std::cout << "Server stopped gracefully.\n";
    return 0;
}