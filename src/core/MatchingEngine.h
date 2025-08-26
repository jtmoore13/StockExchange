#pragma once
#include "ExchangeOrder.h"
#include "SharedTradingTypes.h"
#include "misc/Logger.h"
#include "misc/libraries/concurrentqueue.h"
#include <vector>
#include <list>
#include <unordered_map>

class MatchingEngine : public LoggerBase
{
public:
    MatchingEngine(const std::string& instrument, TradingTypes::MatchingEngineFillEventsQueue& fillEventsQueue);
    virtual ~MatchingEngine();
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    [[nodiscard]] bool AddOrder(std::unique_ptr<ExchangeOrder> order);
    [[nodiscard]] bool CancelOrder(TradingTypes::OrderId orderId);

    void ClearBook();
    void PrintBook() const;

private:
    virtual void Log_(const std::string& msg) const override;

    void LaunchOrderMatchingThread_();

    template <Enums::SideType TSideType>
    void AddOrderToBook_(std::unique_ptr<ExchangeOrder> order, int limitPriceIndex);
    template <Enums::PriceType TPriceType>
    void MatchAggressingSellOrder_(std::unique_ptr<ExchangeOrder> aggressingSellOrder);
    template <Enums::PriceType TPriceType>
    void MatchAggressingBuyOrder_(std::unique_ptr<ExchangeOrder> aggressingBuyOrder);

    void MatchOrders_(std::stop_token stopToken);
    void UpdateQtyAndStatus_(ExchangeOrder& aggressingOrder, ExchangeOrder& restingOrder, unsigned qtyTraded);
    void LogTrade_(TradingTypes::Qty qtyTraded, const ExchangeOrder& aggressingOrder, const ExchangeOrder& restingOrder) const;

    void UpdateBestBidAndAsk_();
    TradingTypes::Price FindBestBid_() const;
    TradingTypes::Price FindBestAsk_() const;
    void LogMarket_() const;

    const std::string instrument_;

    /*
        I think this is a reasonable data structure for our order book.

        Every index in the vector represents a discrete price level, and
        at each index, we store a linked list (std::list) of orders at that price.

        Using a vector for price levels gives us O(1) access time when
        evaluating incoming orders. Since we resize the vector up front,
        we also avoid dynamic resizing during runtime.

        Using a std::list at each price level gives us O(1) insertion
        and deletion, and most importantly, stable iterators, even after
        inserting or removing other elements. This is ideal for a 
        price-time priority book. To support O(1) cancels, we can 
        keep a separate map of OrderId <-> iterator.

        Compared to a deque or vector, std::list has worse cache locality,
        and traversing many orders will be slower. But we probably 
        won't iterate more than a few orders per price level on most match
        attempts, so this tradeoff is probably worth it.

        <shrugs><does_not_want_to_benchmark></shrugs>
    */
    using PriceLevel = std::list<std::unique_ptr<ExchangeOrder>>;
    struct LimitOrderBook {
        std::vector<PriceLevel> bids;
        std::vector<PriceLevel> asks;
        TradingTypes::Price bestBid; 
        TradingTypes::Price bestAsk;
    };
    LimitOrderBook orderBook_;

    std::unordered_map<TradingTypes::OrderId, PriceLevel::iterator> orderIteratorLookupMap_;

    /*
        MatchingEngine <--> Exchange
    
        The exchange passes us this MPMC queue (even tho there's only one producer
        (the single matching engine thread)) so we can dump the raw trade
        info as soon as the trade happens, as quickly as possible. The exchange
        will poll from this queue, and will process/ship the info upstream.
    */
    TradingTypes::MatchingEngineFillEventsQueue& fillEventsQueue_;

    moodycamel::ConcurrentQueue<std::unique_ptr<ExchangeOrder>> ordersQueue_; // MPMC but we only use one consumer
    std::jthread matchingThread_;
};