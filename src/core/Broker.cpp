#include "Broker.h"
#include "Utils/NetworkUtils.h"
#include "Utils/OrderUtils.h"
#include "misc/Colors.h"
#include "misc/proto/ProtoUtils.h"
#include <regex>

using namespace Colors;
using namespace Enums;
using namespace TradingTypes;

namespace {

    constexpr const char* eventColorDefault = White;

    constexpr const char* eventColorGatewayAck = White;
    constexpr const char* eventColorGatewayReject = Red;
    constexpr const char* eventColorFill = PaleGreen;

    constexpr const char* GetStatusColor(OrderStatus status)
    {
        switch (status) {
            case OrderStatus::Unfilled:         return PaleRed;
            case OrderStatus::Filled:           return BoldEmeraldGreen;
            case OrderStatus::Done:             return SteelBlue;
            default:                            return White;
        }
    }

    std::string StripAnsi(const std::string& s) {
        static const std::regex ansiPattern(R"(\x1B\[[0-9;]*m)");
        return std::regex_replace(s, ansiPattern, "");
    }

    std::string PadAnsiAware(const std::string& input, size_t targetLen) {
        const std::string visible = StripAnsi(input);
        const size_t visibleLen = visible.length();
        if (visibleLen >= targetLen)
            return input;
        size_t numSpaces = targetLen - visibleLen;
        return input + std::string(numSpaces, ' ');
    }

    std::string PadHeader(const std::string& input) 
    {
        return PadAnsiAware(input, 12);
    }

    std::string PadCategory(const std::string& input)
    {
        return PadAnsiAware(input, 12);
    }

    std::string PadSymbol(const std::string& input)
    {
        return PadAnsiAware(input, 27);
    }
}


Broker::Broker(const std::string& name, const std::string& corporation) :
    name_(name), corporation_(corporation)
{ }


Broker::~Broker()
{
    if (sockMsgProcessingThread_.joinable()) {
        sockMsgProcessingThread_.request_stop();
        exchangeCxnCv_.notify_one(); // in case it was asleep, let the loop in ProessSocketMsgs() finish
    }
}


bool Broker::IsConnectedToExchange() const
{
    return sock_ && sock_->IsConnected();
}


void Broker::ConnectToExchangeServer(int exchPort, const std::string& exchIp, int reconnectTimeout)
{
    if (!sock_) {
        InitializeSocket_(exchPort, exchIp, reconnectTimeout);
    }
    if (IsConnectedToExchange()) {
        return;
    }
    LOG("Requesting a connection to the exchange...");
    sock_->ConnectToServer();
    if (sock_->IsConnected()) {
        LaunchMsgProcessingThread_();
    }
}


bool Broker::SendMarketOrder(const std::string& instrument, Qty qty, SideType sideType)
{
    return SendOrder_(std::move(instrument), qty, sideType, PriceType::MARKET);
}


bool Broker::SendLimitOrder(const std::string& instrument, Qty qty, double price, SideType sideType)
{
    return SendOrder_(std::move(instrument), qty, sideType, PriceType::LIMIT, price);
}


bool Broker::SendCancel(int orderNum)
{
    proto::CancelOrder cancelOrder;
    cancelOrder.set_order_num(orderNum);
    return SendMessage_(NetworkUtils::CreateNetworkMessageWithType<proto::CancelOrder>(cancelOrder));
}


/*virtual*/ void Broker::Log_(const std::string& msg) const /*override*/
{
    static const std::string prefix = "[" + LoggingPrefix::broker + "]";
    Logger::LogMsg(msg, prefix);
}


bool Broker::SendOrder_(std::string instrument, Qty qty, SideType sideType, PriceType priceType, 
    std::optional<Price> price)
{
    proto::OrderRequest orderRequest;
    orderRequest.set_qty(qty);
    orderRequest.set_instrument(std::move(instrument));
    orderRequest.set_side_type(static_cast<int>(sideType));
    orderRequest.set_price_type(static_cast<int>(priceType));

    if (priceType == PriceType::LIMIT && price.has_value()) {
        orderRequest.set_price(price.value());
    }

    const auto networkMsg = NetworkUtils::CreateNetworkMessageWithType<proto::OrderRequest>(orderRequest);
    if (!SendMessage_(std::move(networkMsg))) {
        LOG_ERROR("Failed to send OrderRequest");
        return false;
    }
    return true;
}


void Broker::InitializeSocket_(int exchPort, const std::string& exchIp, int reconnectTimeout)
{
    sock_ = std::make_unique<TcpClientSocket>(exchPort, exchIp, reconnectTimeout);
    sock_->SetOnDiscoFxn([this]() { OnExchangeDisco_(); });
    sock_->SetOnCxnFxn([this](){ OnExchangeCxn_(); });
}


void Broker::LaunchMsgProcessingThread_()
{
    sockMsgProcessingThread_ = std::jthread([this](std::stop_token token) {
        ProcessSocketMsgs_(token);
    });
}


void Broker::OnExchangeDisco_()
{
    LOG(fmt::format("{}Disconnected from Exchange.{} Attempting to reconnect...", Red, Reset));
}


void Broker::OnExchangeCxn_()
{
    LOG(ColorUtils::Wrap("Connected to Exchange!", Green));
    exchangeCxnCv_.notify_one(); // tell listening thread to wake up and resume processing exchange msgs
}


bool Broker::SendMessage_(proto::ExchangeNetworkMsg networkMsg) const
{
    if (!IsConnectedToExchange()) {
        return false;
    }
    return sock_->SendMessage(networkMsg.SerializeAsString());
}


void Broker::ProcessSocketMsgs_(std::stop_token stopToken)
{
    /*
        In reality, we could probably design the TcpClientSocket better to
        perhaps take in an OnMsgRecvdFxn callback or something, instead of
        asking the caller (Broker) to constantly poll the queue. Polling
        is best option for the ultra high-throughput server socket, but I 
        was too lazy to implement something different for the client. Can come 
        back and do this later.
    */
    LOG("Polling for incoming socket messages...");

    std::unique_lock lock(exchangeCvMutex_);

    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        while (auto msg = sock_->GetNextMessage()) {
            auto networkMsg = ProtoUtils::ExtractNetworkMsgFromSockMsg(msg.value());
            if (networkMsg.has_gateway_ack()) {
                ProcessGatewayAck_(networkMsg.gateway_ack());
            } else if (networkMsg.has_gateway_reject()) {
                ProcessGatewayReject_(networkMsg.gateway_reject());
            } else if (networkMsg.has_status_update()) {
                ProcessStatusUpdate_(networkMsg.status_update());
            } else if (networkMsg.has_fill()) {
                ProcessFill_(networkMsg.fill());
            } else if (networkMsg.has_cancel_order_response()) {
                ProcessCancelResponse_(networkMsg.cancel_order_response());
            }
        }

        // No need for constant polling here. If disconnected from exchange, sleep until woken.
        exchangeCxnCv_.wait(lock, [&]() { 
            return IsConnectedToExchange() || stopToken.stop_requested(); 
        });
    }
}


/*virtual*/ void Broker::ProcessGatewayAck_(const proto::GatewayAck& ack)
{
    PrintRow_(ack.order_id(), ack.symbol(), "Ack", "Ack'd by server", eventColorGatewayAck);
}


/*virtual*/ void Broker::ProcessGatewayReject_(const proto::GatewayReject& reject)
{
    const auto& orderRequest = reject.order_request();
    PrintRow_(orderRequest.order_id(), orderRequest.symbol(), "Nack", "Rejected by ExchangeServer: " + reject.reason(),
        eventColorGatewayReject);
}


/*virtual*/ void Broker::ProcessStatusUpdate_(const proto::OrderStatusUpdate& update)
{
    const OrderStatus newStatus = static_cast<OrderStatus>(update.status());
    std::string detailsToDisplay = GetOrderStatusStr(newStatus);

    const std::string details = update.details();
    if (!details.empty()) {
        detailsToDisplay += " (" + update.details() + ")";
    }
    PrintRow_(update.order_id(), update.symbol(), "New Status", detailsToDisplay, GetStatusColor(newStatus));
}


/*virtual*/ void Broker::ProcessFill_(const proto::Fill& fill)
{
    std::string details = fmt::format("{} @ {:.2f}{}", fill.qty_traded(), fill.price(),
        fill.qty_remaining() == 0 ? "" : fmt::format(" ({} remaining)", fill.qty_remaining()));
    PrintRow_(fill.order_id(), fill.symbol(), "Fill", std::move(details), eventColorFill);
}


/*virtual*/ void Broker::ProcessCancelResponse_(const proto::CancelOrderResponse cancelResponse)
{
    const bool success = cancelResponse.success();
    std::string msg = success ? ColorUtils::Wrap("Cancelled", Green) : ColorUtils::Wrap("Failed to cancel", Red);
    PrintRow_(cancelResponse.order_id(), "", "CancelRequest", std::move(msg));
}


/*virtual*/ void Broker::PrintRow_(std::variant<OrderId, std::string> firstCol, const std::string& symbol,
    std::string category, const std::string& details, const char* rowColor) const
{
    if (!rowColor) {
        rowColor = eventColorDefault;
    }

    // yucky but I liked not having to ask the caller to transform OrderId into a string first each time
    std::string paddedHeader;
    if (const OrderId* orderId = std::get_if<OrderId>(&firstCol)) {
        paddedHeader = PadHeader(OrderUtils::GetOrderNum(*orderId, rowColor == eventColorDefault ? TradingColors::orderId : rowColor));
    } else if (const std::string* text = std::get_if<std::string>(&firstCol)) {
        paddedHeader = ColorUtils::Wrap(PadHeader(*text), rowColor);
    }

    LOG(fmt::format("{} | {} | {} | {}",
        PadSymbol(symbol),
        std::move(paddedHeader),
        ColorUtils::Wrap(PadCategory(category), rowColor),
        ColorUtils::Wrap(details, rowColor)
    ));
}