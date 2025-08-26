#include "core/Exchange.h"
#include "misc/Colors.h"
#include <csignal>
#include <iostream>
#include <poll.h>
#include <unistd.h>

using namespace Colors;
using namespace std::chrono_literals;

namespace {
    std::atomic<bool> doShutdown = false;
}


void SignalHandler(int signal)
{
    if (signal == SIGINT) {
        std::cout << "\n";
        doShutdown.store(true);
    }
}


int main()
{
    std::cout << ClassColors::exchange;
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                           Exchange                           ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << Reset << std::flush;

    std::signal(SIGINT, SignalHandler);

    Exchange exchange;
    exchange.ProcessServerMessages();

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::cout << "\n[Enter <instrument> to view order book]\n" << std::endl;

    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    while (!doShutdown) {
        int ret = poll(&pfd, 1, 100); // 100 ms timeout
        if (ret > 0 && (pfd.revents & POLLIN)) {
            std::string input;
            if (std::getline(std::cin, input)) {
                if (!exchange.PrintBook(input == "" ? "AAPL" : input)) {
                    std::cout << ColorUtils::Wrap(fmt::format("\"{}\" is not a valid or traded instrument", input), Orange) << std::endl;
                }
            }
        }
    }

    return 0;
}
