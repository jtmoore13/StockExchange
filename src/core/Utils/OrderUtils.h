#pragma once
#include "SharedTradingTypes.h"
#include "Utils/ExchangeUtils.h"
#include "misc/Colors.h"
#include "fmt/core.h"


namespace OrderUtils {
    
    inline std::string GetReadableOrderId(TradingTypes::OrderId orderId)
    {
        const auto [sessionId, orderNum] = ExchangeUtils::GetOrderIdComponents(orderId);
        return fmt::format("{}-{}", sessionId, orderNum);
    }
    
    inline std::string GetLoggedOrderId(TradingTypes::OrderId orderId, const char* color = TradingColors::orderId)
    {
        return ColorUtils::Wrap(fmt::format("ID: {}", GetReadableOrderId(orderId)), color);
    }

    inline std::string GetOrderNum(TradingTypes::OrderId orderId, const char* color = TradingColors::orderId)
    {
        return ColorUtils::Wrap(fmt::format("ID: {}", ExchangeUtils::GetOrderNumFromOrderId(orderId)), color);
    } 

    inline TradingTypes::OrderId GenerateOrderId(TradingTypes::SessionId sessionId, TradingTypes::OrderId orderNum)
    {
        /* 
            Top 16 bits are the broker session ID, bottom 48 bits are order 
            number. This format is good for 2^16 = 65,536 simutaneous 
            connections and 2^49 = 281 trillion orders. Plenty for this :)
        */
        uint64_t highBits = static_cast<uint64_t>(sessionId) << 48;
        static constexpr uint64_t LOWER_48_MASK = 0xFFFFFFFFFFFFULL;
        uint64_t lowBits = orderNum & LOWER_48_MASK;
        return highBits | lowBits;
    }
}

