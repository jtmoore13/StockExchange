#pragma once
#include "libraries/concurrentqueue.h"
#include <cstdint>
#include <string>

namespace Enums {

    enum class SideType : uint8_t { 
        BUY, 
        SELL 
    };

    enum class PriceType : uint8_t { 
        LIMIT, 
        MARKET 
    };

    enum class OrderStatus : uint8_t {
        Accepted,
        Rejected,
        PartiallyFilled,
        Filled,
        Unfilled,
        Done
    };

    inline std::string GetPriceTypeStr(PriceType priceType)
    {
        switch (priceType) {
            case PriceType::MARKET:  return "MKT";
            case PriceType::LIMIT:   return "LMT";
            default:                 return "Unknown";
        }
    }

    inline std::string GetSideTypeStr(SideType sideType)
    {
        switch (sideType) {
            case SideType::BUY:   return "BUY";
            case SideType::SELL:  return "SELL";
            default:              return "Unknown";
        }
    }

    inline std::string GetOrderStatusStr(Enums::OrderStatus status)
    {
        switch (status) {
            case OrderStatus::Accepted:         return "Accepted";
            case OrderStatus::Rejected:         return "Rejected";
            case OrderStatus::PartiallyFilled:  return "Partially Filled";
            case OrderStatus::Filled:           return "Filled";
            case OrderStatus::Unfilled:         return "Unfilled";
            case OrderStatus::Done:             return "Done";
            default:                            return "Unknown";
        }
    }
}

namespace TradingTypes {

    using OrderId = uint64_t; // must be uint64_t, do not change (we use 64 bits to store sessionId/orderNum)
    using SessionId = uint16_t;
    using Instrument = std::string;
    using Price = double;
    using Qty = uint32_t;

    struct OrderSnapshot {
        OrderId orderId = 0;
        Qty qty = 0; 
        Qty qtyRemaining = 0;
        std::string symbol;
        Enums::OrderStatus status = Enums::OrderStatus::Accepted;
    };

    struct MatchingEngineFillEvent {
        MatchingEngineFillEvent(Price price_, Qty qty_, OrderSnapshot snapshot_, bool statusChanged_ = true) :
            snapshot(snapshot_),
            price(price_),
            qty(qty_),
            orderStatusChanged(statusChanged_)
        { }
        MatchingEngineFillEvent() = default;

        OrderSnapshot snapshot;
        Price price = 0;
        Qty qty = 0;
        bool orderStatusChanged = true;
    };

    using MatchingEngineFillEventsQueue = moodycamel::ConcurrentQueue<MatchingEngineFillEvent>;
}