#pragma once
#include "ExchangeConfig.h"
#include "SharedTradingTypes.h"
#include "misc/proto/messages.pb.h"

namespace ExchangeUtils {

    inline TradingTypes::SessionId GetSessionIdFromOrderId(TradingTypes::OrderId orderId)
    {
        return static_cast<TradingTypes::SessionId>(orderId >> 48);
    }

    inline TradingTypes::OrderId GetOrderNumFromOrderId(TradingTypes::OrderId orderId)
    {
        static constexpr uint64_t LOWER_48_MASK = 0xFFFFFFFFFFFFULL; // lower 48 bits set to 1
        return static_cast<TradingTypes::OrderId>(orderId & LOWER_48_MASK);
    }

    inline std::pair<TradingTypes::SessionId, uint64_t> GetOrderIdComponents(TradingTypes::OrderId orderId)
    {
        const auto sessionId = GetSessionIdFromOrderId(orderId);
        const auto orderNum = GetOrderNumFromOrderId(orderId);
        return { sessionId, orderNum };
    }

    inline ExchangeIPC::QueueMsg WrapInQueueMsg(const proto::ExchangeNetworkMsg& networkMsg)
    {
        ExchangeIPC::QueueMsg queueMsg;
        queueMsg.msgSize = networkMsg.ByteSizeLong();
        networkMsg.SerializeToArray(queueMsg.msg.data(), queueMsg.msgSize);
        return queueMsg;
    }
}
