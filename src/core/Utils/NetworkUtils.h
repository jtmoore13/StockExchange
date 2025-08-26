#pragma once
#include "misc/proto/messages.pb.h"

namespace NetworkUtils {

    template <typename T>
    proto::ExchangeNetworkMsg CreateNetworkMessageWithType(const T& payload)
    {
        proto::ExchangeNetworkMsg networkMsg;
        if constexpr (std::is_same_v<T, proto::OrderStatusUpdate>) {
            *networkMsg.mutable_status_update() = payload;
        } else if constexpr (std::is_same_v<T, proto::GatewayReject>) {
            *networkMsg.mutable_gateway_reject() = payload;
        } else if constexpr (std::is_same_v<T, proto::GatewayAck>) {
            *networkMsg.mutable_gateway_ack() = payload;
        } else if constexpr (std::is_same_v<T, proto::Fill>) {
            *networkMsg.mutable_fill() = payload;
        } else if constexpr (std::is_same_v<T, proto::OrderRequest>) {
            *networkMsg.mutable_order_request() = payload;
        } else if constexpr (std::is_same_v<T, proto::CancelOrder>) {
            *networkMsg.mutable_cancel_order() = payload;
        } else if constexpr (std::is_same_v<T, proto::CancelOrderResponse>) {
            *networkMsg.mutable_cancel_order_response() = payload;
        } else {
            static_assert(true, "Unsupported type for ExchangeNetworkMsg");
        }
        return networkMsg;
    }
}