#include "MatchingEngine.h"
#include "Utils/OrderUtils.h"
#include "misc/Colors.h"
#include "misc/DebugAssert.h"

using namespace Colors;
using namespace Enums;
using namespace TradingTypes;

namespace {

    // change to true to disable self-trades (false lets us easily test the book with only 1 broker)
    constexpr bool IS_PRODUCTION = false;

    constexpr Price NO_BID = -1;
    constexpr Price NO_ASK = -1;
    constexpr int TICKS_PER_DOLLAR = static_cast<int>(1.0 / ExchangeConfig::TICK_SIZE);

    constexpr auto bidColor = PaleGreen;
    constexpr auto askColor = PaleRed;
    constexpr auto logMarketColor = LightGrey; 

    size_t PriceToIndex(Price price)
    {
        return static_cast<size_t>(price * TICKS_PER_DOLLAR + 0.5);
    }

    Price IndexToPrice(size_t index)
    {
        return index * ExchangeConfig::TICK_SIZE;
    }
}


MatchingEngine::MatchingEngine(const std::string& instrument, MatchingEngineFillEventsQueue& fillEventsQueue) :
    instrument_(instrument),
    fillEventsQueue_(fillEventsQueue)
{
    // need to resize() (not just reserve()) so every index is valid to begin with
    orderBook_.bids.resize((OrderLimits::MAX_ALLOWED_PRICE - OrderLimits::MIN_ALLOWED_PRICE + 1) * TICKS_PER_DOLLAR);
    orderBook_.asks.resize((OrderLimits::MAX_ALLOWED_PRICE - OrderLimits::MIN_ALLOWED_PRICE + 1) * TICKS_PER_DOLLAR);
    
    orderBook_.bestBid = NO_BID;
    orderBook_.bestAsk = NO_ASK;

    orderIteratorLookupMap_.reserve(1'000'000);
    LaunchOrderMatchingThread_();
}


/*virtual*/ MatchingEngine::~MatchingEngine() 
{ 
    matchingThread_.request_stop();
    matchingThread_.join();
}


bool MatchingEngine::AddOrder(std::unique_ptr<ExchangeOrder> order)
{
    DEBUG_ASSERT(order->instrument_ == instrument_);
    std::string orderId = order->readableOrderId_;
    if (!ordersQueue_.try_enqueue(std::move(order))) {
        LOG_WARNING(fmt::format("Failed to add order {} to {} order book", std::move(orderId), instrument_));
        return false;
    }
    return true;
}


bool MatchingEngine::CancelOrder(OrderId orderId)
{    
    auto it = orderIteratorLookupMap_.find(orderId);
    if (it == orderIteratorLookupMap_.end()) {
        LOG_WARNING(fmt::format("Cancel request failed; {} is not a resting order", OrderUtils::GetReadableOrderId(orderId)));
        return false;
    }

    const auto orderIt = it->second;
    const auto& order = *orderIt;

    auto& priceLevels = (order->sideType_ == SideType::BUY) ? orderBook_.bids : orderBook_.asks;
    const auto index = PriceToIndex(order->price_);

    // actually remove it from the book!
    priceLevels[index].erase(orderIt);

    orderIteratorLookupMap_.erase(it);

    UpdateBestBidAndAsk_();
    return true;
}


void MatchingEngine::ClearBook()
{
    const auto SendUnfilledNotification = [&](ExchangeOrder& order) {
        order.status_ = OrderStatus::Done;
        fillEventsQueue_.try_enqueue(MatchingEngineFillEvent(0, 0, order.Snapshot()));
    };

    const auto ClearSide = [&](auto& side) {
        for (auto& priceLevel : side) {
            if (!priceLevel.empty()) {
                for (auto it = priceLevel.begin(); it != priceLevel.end(); ) {
                    ExchangeOrder& order = **it;
                    SendUnfilledNotification(order);
                    it = priceLevel.erase(it);
                }
            }
        }
    };

    ClearSide(orderBook_.bids);
    ClearSide(orderBook_.asks);
    LOG(instrument_ + " order book cleared.");
}


void MatchingEngine::PrintBook() const
{
    static constexpr auto color = SkyBlue;

    std::ostringstream oss;

    oss << color << "\n========== " << ColorUtils::Wrap(instrument_, IceBlue) << color << " Order Book" << " ==========\n" << Reset;

    oss << ColorUtils::Wrap("BIDS:", bidColor) << std::endl;
    const auto& bids = orderBook_.bids;
    for (int i = bids.size() - 1; i >= 0; --i) {
        const auto& bidsAtPrice = bids[i];
        if (!bidsAtPrice.empty()) {
            const auto price = IndexToPrice(i);
            oss << price << ": [";
            for (auto it = bidsAtPrice.begin(); it != bidsAtPrice.end(); ++it) {
                oss << (*it)->qtyRemaining_;
                if (std::next(it) != bidsAtPrice.end()) {
                    oss << ", ";
                }
            }
            oss << "]" << std::endl;
        }
    }

    oss << "\n" << ColorUtils::Wrap("ASKS:", askColor) << std::endl;
    const auto& asks = orderBook_.asks;
    for (size_t i = 0; i < asks.size(); ++i) {
        const auto& asksAtPrice = asks[i];
        if (!asksAtPrice.empty()) {
            const auto price = IndexToPrice(i);
            oss << price << ": [";
            for (auto it = asksAtPrice.begin(); it != asksAtPrice.end(); ++it) {
                oss << (*it)->qtyRemaining_;
                if (std::next(it) != asksAtPrice.end()) {
                    oss << ", ";
                }
            }
            oss << "]" << std::endl;
        }
    }
    oss << color << "=====================================\n" << Reset;

    std::cout << oss.str() << std::endl;
}


/*virtual*/ void MatchingEngine::Log_(const std::string& msg) const /*override*/
{  
    static const std::string prefix = "[" + LoggingPrefix::matchingEngine + "]";
    Logger::LogMsg(msg, prefix);
}


void MatchingEngine::LaunchOrderMatchingThread_()
{
    // we should pin this thread to a core...very important thread!
    matchingThread_ = std::jthread([this](std::stop_token stopToken) {
        MatchOrders_(stopToken);
    });
}


// The compiler is already very good at knowing what to inline, but forcing it to inline this call shouldn't hurt (I think)
__attribute__((always_inline)) inline void MatchingEngine::UpdateQtyAndStatus_(ExchangeOrder& aggressingOrder,
    ExchangeOrder& restingOrder, unsigned qtyTraded)
{
    restingOrder.qtyRemaining_ -= qtyTraded;
    restingOrder.status_ = restingOrder.qtyRemaining_ == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;

    aggressingOrder.qtyRemaining_ -= qtyTraded;
    aggressingOrder.status_ = aggressingOrder.qtyRemaining_ == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
}


void MatchingEngine::LogTrade_(Qty qtyTraded, const ExchangeOrder& aggressingOrder, const ExchangeOrder& restingOrder) const
{
    LOG(ColorUtils::Wrap(fmt::format("TRADE: {} {} @ {} ({} and {})",
        qtyTraded,
        restingOrder.instrument_,
        restingOrder.price_,
        aggressingOrder.readableOrderId_,
        restingOrder.readableOrderId_
    ), Green));
    LOG_VERBOSE(ColorUtils::Wrap("Aggressing:  ", PaleGreen) + aggressingOrder.symbol_); 
    LOG_VERBOSE(ColorUtils::Wrap("Resting:     ", PaleGreen) + restingOrder.symbol_);
}


template <SideType TSideType>
void MatchingEngine::AddOrderToBook_(std::unique_ptr<ExchangeOrder> order, int limitPriceIndex)
{
    auto& priceLevel = (TSideType == SideType::BUY)
        ? orderBook_.bids[limitPriceIndex]
        : orderBook_.asks[limitPriceIndex];

    priceLevel.push_back(std::move(order));
    auto it = std::prev(priceLevel.end());
    orderIteratorLookupMap_[(*it)->orderId_] = it;
}


template <PriceType TPriceType>
void MatchingEngine::MatchAggressingSellOrder_(std::unique_ptr<ExchangeOrder> aggressingSellOrder)
{    
    if constexpr (TPriceType == PriceType::MARKET) {
        if (orderBook_.bestBid == NO_BID) [[unlikely]] {
            return;
        }
    }

    int limitPriceIndex;
    if constexpr (TPriceType == PriceType::LIMIT) {
        limitPriceIndex = PriceToIndex(aggressingSellOrder->price_);
    }

    // start taking qty at the best price
    for (int i = PriceToIndex(orderBook_.bestBid); i >= 0; --i) {

        if constexpr (TPriceType == PriceType::LIMIT) {
            if (i < limitPriceIndex) {
                break;
            }
        }
        auto& restingBuyOrdersAtPx = orderBook_.bids[i];
        if (restingBuyOrdersAtPx.empty()) {
            continue;
        }

        // we group fills on the aggressing order by price to reduce notifications
        Qty qtyTradedAtPrice = 0;
        const Price price = IndexToPrice(i);

        // iterate the linked list of orders, which are already sorted by time
        for (auto it = restingBuyOrdersAtPx.begin(); it != restingBuyOrdersAtPx.end();) {
            auto& restingBuyOrder = *it;
        
            if constexpr (IS_PRODUCTION) {
                // can't trade with yourself! (in prod)
                if (restingBuyOrder->sessionId_ == aggressingSellOrder->sessionId_) {
                    ++it; continue;
                }
            }

            Qty qtyTraded = std::min(restingBuyOrder->qtyRemaining_, aggressingSellOrder->qtyRemaining_);
            LogTrade_(qtyTraded, *aggressingSellOrder, *restingBuyOrder);
            
            DEBUG_ASSERT(qtyTraded > 0);
            DEBUG_ASSERT(restingBuyOrder->priceType_ == PriceType::LIMIT);

            qtyTradedAtPrice += qtyTraded;

            // When creating a MatchingEngineFillEvent a couple lines down, we need to know
            // if the resting order's status changed. The only way it could have changed here is if it's
            // going from Accepted --> Partially Filled.
            const bool restingOrderHadPrevFills = restingBuyOrder->status_ == OrderStatus::PartiallyFilled;

            UpdateQtyAndStatus_(*aggressingSellOrder, *restingBuyOrder, qtyTraded);
            const bool restingOrderStatusChanged = restingBuyOrder->status_ == OrderStatus::Filled || !restingOrderHadPrevFills;
            fillEventsQueue_.try_enqueue(MatchingEngineFillEvent(price, qtyTraded, restingBuyOrder->Snapshot(), restingOrderStatusChanged));

            if (restingBuyOrder->qtyRemaining_ == 0) {
                orderIteratorLookupMap_.erase(restingBuyOrder->orderId_);
                it = restingBuyOrdersAtPx.erase(it);
            } else {
                ++it;
            }

            // aggressing order completely filled, woo!
            if (aggressingSellOrder->qtyRemaining_ == 0) {
                const bool filledSomeQty = aggressingSellOrder->qty_ != aggressingSellOrder->qtyRemaining_;
                fillEventsQueue_.try_enqueue(MatchingEngineFillEvent(price, qtyTradedAtPrice, aggressingSellOrder->Snapshot(), filledSomeQty));
                return;
            }
        }

        DEBUG_ASSERT(aggressingSellOrder->qtyRemaining_ > 0);

        if (qtyTradedAtPrice > 0) {
            fillEventsQueue_.try_enqueue(MatchingEngineFillEvent(price, qtyTradedAtPrice, aggressingSellOrder->Snapshot(), true));
        }
    }

    // rare, but mark market orders that weren't filled at all as 'Unfilled'
    if constexpr (TPriceType == PriceType::MARKET) {
        if (aggressingSellOrder->qty_ == aggressingSellOrder->qtyRemaining_) {
            aggressingSellOrder->status_ = OrderStatus::Unfilled;
            fillEventsQueue_.try_enqueue(MatchingEngineFillEvent(0, 0, aggressingSellOrder->Snapshot()));
            return;
        }
    }

    DEBUG_ASSERT(aggressingSellOrder->qtyRemaining_ > 0);
    
    if constexpr (TPriceType == PriceType::LIMIT) {
        AddOrderToBook_<SideType::SELL>(std::move(aggressingSellOrder), limitPriceIndex);
    }
}


template <Enums::PriceType TPriceType>
void MatchingEngine::MatchAggressingBuyOrder_(std::unique_ptr<ExchangeOrder> aggressingBuyOrder)
{
    if constexpr (TPriceType == PriceType::MARKET) {
        if (orderBook_.bestAsk == NO_ASK) [[unlikely]] {
            return;
        }
    }

    size_t limitPriceIndex;
    if constexpr (TPriceType == PriceType::LIMIT) {
        limitPriceIndex = PriceToIndex(aggressingBuyOrder->price_);
    }

    for (size_t i = PriceToIndex(orderBook_.bestAsk); i < orderBook_.asks.size(); ++i) {

        if constexpr (TPriceType == PriceType::LIMIT) {
            if (i > limitPriceIndex) {
                break;
            }
        }
    
        auto& restingSellOrdersAtPx = orderBook_.asks[i];
        if (restingSellOrdersAtPx.empty()) {
            continue;
        }
    
        Qty qtyTradedAtPrice = 0;
        const Price price = IndexToPrice(i);

        for (auto it = restingSellOrdersAtPx.begin(); it != restingSellOrdersAtPx.end();) {
            auto& restingSellOrder = *it;

            if constexpr (IS_PRODUCTION) {
                if (restingSellOrder->sessionId_ == aggressingBuyOrder->sessionId_) {
                    ++it; continue;
                }
            }

            Qty qtyTraded = std::min(restingSellOrder->qtyRemaining_, aggressingBuyOrder->qtyRemaining_);
            DEBUG_ASSERT(qtyTraded > 0);
            LogTrade_(qtyTraded, *aggressingBuyOrder, *restingSellOrder);
            qtyTradedAtPrice += qtyTraded;

            const bool restingOrderHadPrevFills = restingSellOrder->status_ == OrderStatus::PartiallyFilled;
            UpdateQtyAndStatus_(*aggressingBuyOrder, *restingSellOrder, qtyTraded);
            const bool restingOrderStatusChanged = restingSellOrder->status_ == OrderStatus::Filled || !restingOrderHadPrevFills;
            fillEventsQueue_.try_enqueue(MatchingEngineFillEvent(price, qtyTraded, restingSellOrder->Snapshot(), restingOrderStatusChanged));

            if (restingSellOrder->qtyRemaining_ == 0) {
                orderIteratorLookupMap_.erase(restingSellOrder->orderId_);
                it = restingSellOrdersAtPx.erase(it);
            } else {
                ++it;
            }

            if (aggressingBuyOrder->qtyRemaining_ == 0) {
                const bool filledSomeQty = aggressingBuyOrder->qty_ != aggressingBuyOrder->qtyRemaining_;
                fillEventsQueue_.try_enqueue(MatchingEngineFillEvent(price, qtyTradedAtPrice, aggressingBuyOrder->Snapshot(), filledSomeQty));
                return;
            }
        }

        DEBUG_ASSERT(aggressingBuyOrder->qtyRemaining_ > 0);
        if (qtyTradedAtPrice > 0) {
            fillEventsQueue_.try_enqueue(MatchingEngineFillEvent(price, qtyTradedAtPrice, aggressingBuyOrder->Snapshot(), true));
        }
    }

    if constexpr (TPriceType == PriceType::MARKET) {
        if (aggressingBuyOrder->qty_ == aggressingBuyOrder->qtyRemaining_) {
            aggressingBuyOrder->status_ = OrderStatus::Unfilled;
            fillEventsQueue_.try_enqueue(MatchingEngineFillEvent(0, 0, aggressingBuyOrder->Snapshot()));
            return;
        }
    }

    DEBUG_ASSERT(aggressingBuyOrder->qtyRemaining_ > 0);

    if constexpr (TPriceType == PriceType::LIMIT) {
        AddOrderToBook_<SideType::BUY>(std::move(aggressingBuyOrder), limitPriceIndex);
    }
}


void MatchingEngine::MatchOrders_(std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        std::unique_ptr<ExchangeOrder> order;
        if (ordersQueue_.try_dequeue(order)) {
            /*
                Ugly branching here but we want as much code known at compile-time
                as possible. We want to minimize branching in the meat of the
                matching logic to be as quick as possible during runtime.
            */
            if (order->sideType_ == SideType::BUY) {
                if (order->priceType_ == PriceType::LIMIT) {
                   MatchAggressingBuyOrder_<PriceType::LIMIT>(std::move(order));
                } else {
                    MatchAggressingBuyOrder_<PriceType::MARKET>(std::move(order));
                }
            } else {
                if (order->priceType_ == PriceType::LIMIT) {
                    MatchAggressingSellOrder_<PriceType::LIMIT>(std::move(order));
                } else {
                    MatchAggressingSellOrder_<PriceType::MARKET>(std::move(order));
                }
            }
            UpdateBestBidAndAsk_();
        }
    }
}


void MatchingEngine::UpdateBestBidAndAsk_()
{
    /*
        It is certainly inefficient to find the best bid/ask by scanning every
        possible price like we do here. But to keep the matching functions looking
        cleaner (and laziness :)), I decided to do so. If we wanted to be
        performant, we would just update the best bid/ask when either:

        1) The last resting order of a price level is removed (either thru a fill or a cancel)
        2) We insert an order into the order book
    */
    orderBook_.bestBid = FindBestBid_();
    orderBook_.bestAsk = FindBestAsk_();
    LogMarket_();
}


Price MatchingEngine::FindBestBid_() const
{
    for (int i = orderBook_.bids.size() - 1; i >= 0; i--) {
        auto& bidsAtPrice = orderBook_.bids[i];
        if (!bidsAtPrice.empty()) {
            return bidsAtPrice.front()->price_;
        }
    }
    return NO_BID;
}


Price MatchingEngine::FindBestAsk_() const
{
    for (size_t i = 0; i < orderBook_.asks.size(); ++i) {
        auto& asksAtPrice = orderBook_.asks[i];
        if (!asksAtPrice.empty()) {
            return asksAtPrice.front()->price_;
        }
    }
    return NO_ASK;
}


void MatchingEngine::LogMarket_() const
{
    const Price bestBid = orderBook_.bestBid;
    const Price bestAsk = orderBook_.bestAsk;

    const bool isCrossed = (bestBid >= bestAsk && bestBid > 0 && bestAsk > 0);
    std::string crossedMsg = isCrossed
        ? ColorUtils::Wrap(" CROSSED MARKET", bestBid > bestAsk ? Red : Orange)
        : "";

    static constexpr auto noMarketColor = BoldFireRed;
    std::string bestBidStr = bestBid == NO_BID ? ColorUtils::Wrap("NO BID", noMarketColor) : fmt::format("{:.2f}", bestBid);
    std::string bestAskStr = bestAsk == NO_ASK ? ColorUtils::Wrap("NO ASK", noMarketColor) : fmt::format("{:.2f}", bestAsk);

    const std::string msg = fmt::format(
        "{} now {}{}{} {} {}{}{}{}",
        instrument_,
        bidColor, std::move(bestBidStr), Reset,
        ColorUtils::Wrap("@", logMarketColor),
        askColor, std::move(bestAskStr), Reset,
        std::move(crossedMsg)
    );

    LOG(ColorUtils::Wrap(msg, logMarketColor));
}