#include "core/Broker.h"
#include "misc/Colors.h"
#include <random>
#include <iostream>
#include <regex>

using namespace Colors;
using namespace Enums;

namespace {
    std::atomic<bool> doShutdown = false;

    constexpr const char* helpString = 
    R"(
    Order Entry Console
    -------------------
    <Enter>           → Send a completely random order
    "AUTO"            → Start continuous random trades

    Manual Commands:
    BUY  <qty> <symbol>           (send market order)
    SELL <qty> <symbol>           (send market order)
    BUY  <qty> <symbol> LMT <px>  (send limit order)
    SELL <qty> <symbol> LMT <px>  (send limit order)
    CANCEL <order_id>             (cancel resting order)

    Examples:
    BUY 100 AAPL
    SELL 50 MSFT LMT 312.50
    CANCEL 52
    
    Ctrl+C to exit
    )";
}

void SendRandomOrder(Broker& broker)
{
    static std::default_random_engine engine(std::random_device{}());

    const std::vector<std::string> symbols = {"AAPL"}; // just for easy testing

    std::uniform_int_distribution<int> symbolDist(0, symbols.size() - 1);
    std::uniform_int_distribution<int> qtyDist(1, 20);
    std::uniform_real_distribution<double> priceDist(20.0, 20.10);

    static constexpr int sell = 0;
    static constexpr int buy = 1;
    std::uniform_int_distribution<int> sideDist(sell, buy);

    static constexpr int marketOrder = 1;
    static constexpr int limitOrder = 2;
    std::uniform_int_distribution<int> priceTypeDist(marketOrder, limitOrder);

    const std::string symbol = symbols[symbolDist(engine)];
    const double price = std::round(priceDist(engine) * 100.0) / 100.0; // round to 2 decimals
    const SideType side = static_cast<SideType>(sideDist(engine));

    if (priceTypeDist(engine) == 0) {
        broker.SendMarketOrder(symbol, 1, side); // just make qty 1 so we dont eat a bunch of order book qty (for testing)
    } else {
        int qty = qtyDist(engine);
        broker.SendLimitOrder(symbol, qty, price, side);
    }
}


void AutoTrader(Broker& broker)
{
    while (!doShutdown.load()) {
        SendRandomOrder(broker);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
}


void SendBurst(Broker& broker, int numOrders)
{
    for (int i = 0; i < numOrders; ++i) {
        SendRandomOrder(broker);
    }
}


std::optional<int> ParseCancelString(const std::string& input)
{
    static const std::regex pattern(R"(CANCEL\s(\d+))", std::regex::icase);
    std::smatch match;

    if (std::regex_match(input, match, pattern)) {
        return std::stoi(match[1].str());
    }
    return std::nullopt;
}


struct ParsedOrder {
    std::string instrument;
    TradingTypes::Qty qty;
    SideType sideType;
    PriceType priceType;
    std::optional<TradingTypes::Price> price; // only set for LMT
};

std::optional<ParsedOrder> ParseOrderString(const std::string& input) {
    std::regex orderRegex(R"(^\s*(BUY|SELL)\s+(\d+)\s+([A-Z]+)(?:\s+LMT\s+(\d+(?:\.\d+)?))?\s*$)",
                          std::regex::icase);
    std::smatch match;
    if (std::regex_match(input, match, orderRegex)) {
        ParsedOrder order;
        order.sideType = match[1] == "BUY" ? SideType::BUY : SideType::SELL;
        order.qty = std::stoi(match[2]);
        order.instrument = match[3];
        if (match[4].matched) {
            order.priceType = PriceType::LIMIT;
            order.price = std::stod(match[4]);
        } else {
            order.priceType = PriceType::MARKET;
        }
        return order;
    }
    return std::nullopt;
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
    std::cout << ClassColors::broker;
    std::cout << "╔═════════════════════════════════════════════╗\n";
    std::cout << "║                    Broker                   ║\n";
    std::cout << "╚═════════════════════════════════════════════╝\n";
    std::cout << Reset;

    std::signal(SIGINT, SignalHandler);

    Broker broker("Justin Moore", "Insider Trading LLC");
    broker.ConnectToExchangeServer(5400, "127.0.0.1", 10);

    if (!broker.IsConnectedToExchange()) {
        return 0;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    std::cout << "\n[Enter \"HELP\" to see input options]\n\n";

    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    while (!doShutdown) {
    
        int ret = poll(&pfd, 1, 100); // 100 ms timeout
        if (ret > 0 && (pfd.revents & POLLIN)) {

            std::string input;
            if (std::getline(std::cin, input)) {
                if (input.empty()) {
                    SendRandomOrder(broker);
                }
                else if (input == "AUTO") {
                    AutoTrader(broker); // send orders forever
                }
                else if (auto order = ParseOrderString(input)) {
                    if (order->priceType == PriceType::LIMIT) {
                        broker.SendLimitOrder(order->instrument, order->qty, order->price.value(), order->sideType);
                    } else {
                        broker.SendMarketOrder(order->instrument, order->qty, order->sideType);
                    }
                }
                else if (auto orderNum = ParseCancelString(input)) {
                    broker.SendCancel(*orderNum);
                }
                else if (input == "HELP") {
                    std::cout << helpString << std::endl;
                }
                else {
                    std::cout << ColorUtils::Wrap(fmt::format("\"{}\" not a recognized command.", input), Orange) << std::endl;
                }
            }
        }
    }

    return 0;
}
