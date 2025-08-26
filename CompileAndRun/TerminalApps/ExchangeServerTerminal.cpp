#include "core/ExchangeServer.h"
#include "misc/Colors.h"
#include <csignal>
#include <poll.h>

using namespace Colors;

namespace {
    std::atomic<bool> doShutdown = false;
}


void SignalHandler(int signal)
{
    if (signal == SIGINT) {
        std::cout << "\n";
        doShutdown.store(true, std::memory_order_relaxed);
    }
}


int main() 
{
    std::cout << ClassColors::exchangeServer;
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                       Exchange Server                        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << Reset;

    ExchangeServer exchangeServer({"AAPL", "GOOG", "MSFT", "TSLA"});
    exchangeServer.EnableSharedMemoryCleanupOnShutdown();
    exchangeServer.StartListening(5400, "127.0.0.1");

    std::signal(SIGINT, SignalHandler);

    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    // entering anything toggles exchange open/closed, cntrl-c exits
    while (!doShutdown) {
        int ret = poll(&pfd, 1, 100); // 100 ms timeout
        if (ret > 0 && (pfd.revents & POLLIN)) {
            std::string input;
            if (std::getline(std::cin, input)) {
                exchangeServer.SetOpen(!exchangeServer.IsOpen());
            }
        }
    }

    return 0;
}