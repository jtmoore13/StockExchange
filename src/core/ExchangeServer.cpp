#include "ExchangeServer.h"
#include "Utils/OrderUtils.h"
#include "Utils/NetworkUtils.h"
#include "misc/Colors.h"
#include "misc/DebugAssert.h"
#include "misc/Logger.h"
#include "misc/TcpSocket/TcpServerSocket.h"
#include "misc/proto/messages.pb.h"
#include "misc/proto/ProtoUtils.h"
#include "fmt/core.h"

using namespace Colors;
using namespace TradingTypes;
using namespace Enums;

namespace {

    constexpr int numSockThreads = 2;

    std::string GetMarketOpenStr(bool isOpen)
    {
        const char* color = isOpen ? Green : Red;
        const char* openClosed = isOpen ? "OPEN" : "CLOSED";
        return fmt::format("{}{}{}", color, openClosed, Reset);
    }

    std::string BuildSymbol(const proto::OrderRequest& orderRequest)
    {
        static constexpr const char* orderFontColor = Grey;

        const auto sideType = static_cast<SideType>(orderRequest.side_type());
        const auto priceType = static_cast<PriceType>(orderRequest.price_type());
        const auto qty = orderRequest.qty();
        const auto instrument = orderRequest.instrument();
        const auto price = orderRequest.price();

        const char* sideColor = (sideType == SideType::BUY) ? TradingColors::buy : TradingColors::sell;
        switch (priceType) {
            case PriceType::LIMIT:
                return ColorUtils::Wrap(
                        fmt::format("{} {}{}{} {} @ {:.2f} {}", 
                            instrument,
                            sideColor,
                            GetSideTypeStr(sideType),
                            orderFontColor,
                            qty,
                            price, 
                            GetPriceTypeStr(PriceType::LIMIT)), 
                    orderFontColor);
            
            case PriceType::MARKET:
                return ColorUtils::Wrap(
                    fmt::format("{} {}{}{} {} @ {}",
                        instrument,
                        sideColor, 
                        GetSideTypeStr(sideType),
                        orderFontColor,
                        qty,
                        GetPriceTypeStr(PriceType::MARKET)),
                    orderFontColor);
        }
        return "Invalid symbol";
    }

    ExchangeIPC::SupportedInstrumentsArray CreateInstrumentsArray(const std::unordered_set<std::string>& set) {
        ExchangeIPC::SupportedInstrumentsArray array;
        unsigned i = 0;
        for (const std::string& instrument : set) {
            std::memcpy(array[i++], instrument.data(), OrderLimits::MAX_INSTRUMENT_LEN);
            array[i][OrderLimits::MAX_INSTRUMENT_LEN] = '\0';
        }
        return array;
    }
}


ExchangeServer::ExchangeServer(const std::unordered_set<std::string>& supportedInstruments) :
    supportedInstruments_(supportedInstruments),
    shrMem_(ExchangeIPC::SHM_SIZE, ExchangeIPC::SHM_FILENAME, true)
{
    if (supportedInstruments.size() > ExchangeConfig::MAX_INSTRUMENTS_SUPPORTED) {
        throw std::runtime_error(fmt::format("Failed to initialize: max instruments allowed: {}", ExchangeConfig::MAX_INSTRUMENTS_SUPPORTED));
    }
    InitializeSharedMemory_();
    LaunchSendMsgsToBrokersThread_(); // responsible for relaying messages back to brokers
}


/*virtual*/ ExchangeServer::~ExchangeServer()
{ 
    SetOpen(false);

    sockMsgProcessingThread_.request_stop();
    sendMsgsToBrokersThread_.request_stop();
    exchangeMsgProcessingThread_.request_stop();

    sockMsgProcessingThread_.join();
    sendMsgsToBrokersThread_.join();
    exchangeMsgProcessingThread_.join();
}


void ExchangeServer::StartListening(int port, const std::string& ip)
{
    if (!sock_) {
        InitializeSocket_(port, ip);
    }
    if (sock_->IsListening()) {
        return;
    }

    SetOpen(true);
    sock_->StartListening();
    LaunchSockMsgProcessingThread_();
    LaunchExchMsgProcessingThread_();
}


void ExchangeServer::SetOpen(bool isOpen) 
{
    bool old = shmWithExchange_->isOpen.exchange(isOpen);
    if (old != isOpen) {
        LOG(GetMarketOpenStr(isOpen));
    }
}


bool ExchangeServer::IsOpen() const
{
    return shmWithExchange_->isOpen.load();
}


void ExchangeServer::EnableSharedMemoryCleanupOnShutdown()
{
    shrMem_.SetShouldUnmap(true);
}


/*virtual*/ void ExchangeServer::Log_(const std::string& msg) const /*override*/
{
    static const std::string prefix = "[" + LoggingPrefix::exchangeServer + "]";
    Logger::LogMsg(msg, prefix);
}


void ExchangeServer::InitializeSocket_(int port, const std::string& ip)
{
    sock_ = std::make_unique<TcpServerSocket>(port, std::move(ip), numSockThreads);
    sock_->SetOnNewCxnFxn([this](int cxnFd) { OnNewCxn_(cxnFd); });
    sock_->SetOnClientDiscoFxn([this](int discoCxnFd) { OnBrokerDiscxn_(discoCxnFd); } );
}


void ExchangeServer::InitializeSharedMemory_()
{
    shmWithExchange_ = new (shrMem_.Get()) ExchangeIPC::Layout{};

    shmWithExchange_->isOpen.store(true);
    shmWithExchange_->supportedInstrumentsSize = supportedInstruments_.size();
    shmWithExchange_->supportedInstruments = CreateInstrumentsArray(supportedInstruments_);
}


void ExchangeServer::LaunchSendMsgsToBrokersThread_()
{
    sendMsgsToBrokersThread_ = std::jthread([&](std::stop_token stopToken) {
        SendMsgsToBroker_(stopToken);
    });
}


void ExchangeServer::EnqueueNetworkMsg_(const proto::ExchangeNetworkMsg& networkMsg, int destFd)
{
    BrokerBoundMsg msg = { networkMsg.SerializeAsString(), destFd };
    brokerBoundMsgs_.try_enqueue(std::move(msg));
}


void ExchangeServer::EnqueueOrderStatusUpdateNotification_(proto::ExchangeNetworkMsg networkMsg)
{
    OrderId orderId = networkMsg.status_update().order_id();
    auto destFd = GetDestinationFd_(orderId);
    if (!destFd.has_value()) {
        [[maybe_unused]] SessionId sessionId = ExchangeUtils::GetSessionIdFromOrderId(orderId);
        LOG_WARNING(fmt::format("SessionId {} no longer active; dropping OrderStatusUpdate notification", sessionId));
        return;
    }
    EnqueueNetworkMsg_(std::move(networkMsg), destFd.value());
}


void ExchangeServer::EnqueueFillNotification_(proto::ExchangeNetworkMsg networkMsg)
{
    OrderId orderId = networkMsg.fill().order_id();
    auto destFd = GetDestinationFd_(orderId);
    if (!destFd.has_value()) {
        [[maybe_unused]] SessionId sessionId = ExchangeUtils::GetSessionIdFromOrderId(orderId);
        LOG_WARNING(fmt::format("SessionId {} no longer active; dropping Fill notification", sessionId));
        return;
    }
    EnqueueNetworkMsg_(std::move(networkMsg), destFd.value());
}


void ExchangeServer::EnqueueGatewayRejectNotification_(const proto::OrderRequest& rejectedRequest, std::string error, int destFd)
{
    // We have the destFd already here, so no need to look it up like similar
    // functions do. If the destFd cxn disconnects by the time we finally send it,
    // the msg will just get dropped. Not a huge deal. 

    proto::GatewayReject reject;
    reject.set_reason(std::move(error));
    *reject.mutable_order_request() = rejectedRequest;

    auto networkMsg = NetworkUtils::CreateNetworkMessageWithType<proto::GatewayReject>(reject);
    EnqueueNetworkMsg_(std::move(networkMsg), destFd);
}


void ExchangeServer::EnqueueGatewayAckNotification_(const proto::OrderRequest& acceptedRequest, OrderId orderId)
{
    auto destFd = GetDestinationFd_(orderId);
    if (!destFd.has_value()) {
        [[maybe_unused]] SessionId sessionId = ExchangeUtils::GetSessionIdFromOrderId(orderId);
        LOG_WARNING(fmt::format("SessionId {} no longer active; dropping GatewayAck notification", sessionId));
        return; 
    }

    proto::GatewayAck ack;
    ack.set_order_id(orderId);
    ack.set_symbol(BuildSymbol(acceptedRequest));
    auto networkMsg = NetworkUtils::CreateNetworkMessageWithType<proto::GatewayAck>(ack);
    EnqueueNetworkMsg_(std::move(networkMsg), destFd.value());
}


void ExchangeServer::EnqueueCancelOrderResponseNotification_(proto::ExchangeNetworkMsg networkMsg)
{
    auto& cancelResponse = networkMsg.cancel_order_response();

    const OrderId orderId = cancelResponse.order_id();
    auto destFd = GetDestinationFd_(orderId);
    if (!destFd.has_value()) {
        [[maybe_unused]] SessionId sessionId = ExchangeUtils::GetSessionIdFromOrderId(orderId);
        LOG_WARNING(fmt::format("SessionId {} no longer active; dropping GatewayAck notification", sessionId));
        return; 
    }

    EnqueueNetworkMsg_(std::move(networkMsg), destFd.value());
}


void ExchangeServer::OnNewCxn_(int newCxnFd)
{
    std::unique_lock lock(sessionsMutex_);
    SessionId newCxnId = ++allTimeCxns_;
    sessionIdsMap_.insert({newCxnId, newCxnFd});
    lock.unlock();

    LOG(fmt::format("{} (SessionId: {})", ColorUtils::Wrap("New session!", Green), newCxnId, newCxnFd));
}


void ExchangeServer::OnBrokerDiscxn_(int disconnectingCxnFd)
{
    SessionId sessionId;
    {
        std::lock_guard lock(sessionsMutex_);
        sessionId = sessionIdsMap_.right.at(disconnectingCxnFd);
        sessionIdsMap_.right.erase(disconnectingCxnFd);
    }
    LOG(fmt::format("{} (SessionId {})", ColorUtils::Wrap("Broker disconnected", PaleRed), sessionId));
}


std::optional<SessionId> ExchangeServer::LookupSessionId_(int cxnFd) const
{
    std::lock_guard lock(sessionsMutex_);
    auto it = sessionIdsMap_.right.find(cxnFd);
    if (it == sessionIdsMap_.right.end()) {
        return std::nullopt;
    }
    return it->second;
}


std::optional<int> ExchangeServer::GetDestinationFd_(OrderId orderId) const
{
    // Step one: extract broker session ID from order ID (top 16 bits)
    SessionId sessionId = ExchangeUtils::GetSessionIdFromOrderId(orderId);

    // Step two: check the map to see what fd that broker is connected to (if still connected)
    std::lock_guard lock(sessionsMutex_);
    auto it = sessionIdsMap_.left.find(sessionId);
    if (it == sessionIdsMap_.left.end()) {
        return std::nullopt; // sending broker is disconnected, don't worry about updating anyone
    }
    return it->second;
}


void ExchangeServer::LaunchSockMsgProcessingThread_()
{
    if (sockMsgProcessingThread_.joinable()) {
        return;
    }
    sockMsgProcessingThread_ = std::jthread([&](std::stop_token stopToken) {
        ProcessIncomingSockMsgs_(stopToken);
    });
}


void ExchangeServer::LaunchExchMsgProcessingThread_()
{
    if (exchangeMsgProcessingThread_.joinable()) {
        return;
    }
    exchangeMsgProcessingThread_ = std::jthread([&](std::stop_token stopToken){
        ProcessExchangeMsgs_(stopToken);
    });
}


void ExchangeServer::ProcessExchangeMsgs_(std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        ExchangeIPC::QueueMsg queueMsg;
        if (shmWithExchange_->exchangeToServerQueue.Dequeue(queueMsg)) {
            proto::ExchangeNetworkMsg networkMsg;
            if (!networkMsg.ParseFromArray(queueMsg.msg.data(), queueMsg.msgSize)) {
                LOG_ERROR("Failed to parse exchange message");
                continue;
            }
            if (networkMsg.has_status_update()) {
                EnqueueOrderStatusUpdateNotification_(std::move(networkMsg));
            } 
            else if (networkMsg.has_fill()) {
                EnqueueFillNotification_(std::move(networkMsg));
            }
            else if (networkMsg.has_cancel_order_response()) {
                EnqueueCancelOrderResponseNotification_(std::move(networkMsg));
            }
        }
    }
}


void ExchangeServer::AddToExchangeQueue_(proto::ExchangeNetworkMsg networkMsg)
{
    DEBUG_ASSERT(networkMsg.has_cancel_order() || networkMsg.has_order_request());

    ExchangeIPC::QueueMsg queueMsg;
    queueMsg.msgSize = networkMsg.ByteSizeLong();
    networkMsg.SerializePartialToArray(queueMsg.msg.data(), queueMsg.msgSize);
    
    if (!shmWithExchange_->serverToExchangeQueue.Enqueue(queueMsg)) {
        LOG_ERROR(fmt::format("Failed to enqueue {}", networkMsg.has_order_request() ? "CancelOrder" : "OrderRequest"));
    }
}


void ExchangeServer::ProcessIncomingSockMsgs_(std::stop_token stopToken)
{
    /*
        We are going to process orders sequentially on a single thread in to 
        preserve the order they arrived in. We *could* process orders
        in a thread pool, then sort by arrival time, before enqueueing 
        them into the single-threaded matching engine's queue. But until
        we face throughput problems, we can do it sequentially.
    */

    LOG("Polling for socket messages...");

    unsigned totalOrders = 0;

    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (const auto msg = sock_->GetNextMessage()) {
            const auto& sockMsg = msg.value();

            auto sessionId = LookupSessionId_(sockMsg.cxnFd);
            if (!sessionId.has_value()) {
                continue; // broker already disconnected, ignore the request (janky yes, but fine for this)
            }

            auto networkMsg = ProtoUtils::ExtractNetworkMsgFromSockMsg(sockMsg);

            if (networkMsg.has_order_request()) [[likely]] {
                proto::OrderRequest* orderRequest = networkMsg.mutable_order_request();
                orderRequest->set_symbol(BuildSymbol(*orderRequest));

                // validate the order info and accept it if valid
                if (std::string error = ValidateOrderData_(*orderRequest); !error.empty()) {
                    EnqueueGatewayRejectNotification_(*orderRequest, std::move(error), sockMsg.cxnFd);
                    continue;
                }

                // Order request accepted! Assign it an order ID and put it in the exchange's queue
                OrderId orderId = OrderUtils::GenerateOrderId(sessionId.value(), ++totalOrders);
                orderRequest->set_order_id(orderId);

                // send the order to the exchange
                AddToExchangeQueue_(networkMsg);

                // let broker know the order has been accepted
                EnqueueGatewayAckNotification_(*orderRequest, orderId);
            }
            else if (networkMsg.has_cancel_order()) {
                networkMsg.mutable_cancel_order()->set_session_id(sessionId.value());
                AddToExchangeQueue_(std::move(networkMsg));
            }
        }
    }
}


std::string ExchangeServer::ValidateOrderData_(const proto::OrderRequest& order) const
{
    using namespace OrderLimits;

    if (!shmWithExchange_->isOpen.load()) {
        return "Exchange is closed";
    }

    static auto IsAsciiOnly = [](const std::string& str) -> bool {
        return std::all_of(str.begin(), str.end(), [](unsigned char c) {
            return c <= 0x7F;
        });
    };

    // instrument checks
    const std::string& instrument = order.instrument();
    if (!IsAsciiOnly(instrument)) {
        return "Instrument contains non-ASCII characters";
    }
    if (instrument.empty()) {
        return "Instrument field is empty";
    }
    if (!supportedInstruments_.contains(instrument)) {
        return fmt::format("{} not traded on this exchange", instrument);
    }

    // price checks
    const auto priceType = static_cast<PriceType>(order.price_type());
    if (priceType != PriceType::LIMIT && priceType != PriceType::MARKET) {
        return "Invalid price_type value";
    }
    if (priceType == PriceType::LIMIT) {
        const auto price = order.price();
        if (price <= 0) {
            return "Must specify limit order price";
        }
        if (price < OrderLimits::MIN_ALLOWED_PRICE) {
            return "Limit price must exceed Exchange min price of " + fmt::format("{:.2f}", OrderLimits::MIN_ALLOWED_PRICE);
        }
        if (price > OrderLimits::MAX_ALLOWED_PRICE) {
            return "Limit price exceeds Exchange max price of " + fmt::format("{:.2f}", OrderLimits::MAX_ALLOWED_PRICE);
        }
    }

    // qty checks
    const Price qty = order.qty();
    if (qty < MIN_ALLOWED_QTY || qty > MAX_ALLOWED_QTY) {
        return fmt::format("qty must be between {} and {}", MIN_ALLOWED_QTY, MAX_ALLOWED_QTY);
    }

    return "";
}


void ExchangeServer::SendMsgsToBroker_(std::stop_token stopToken)
{
    /*
        Very important note: moodycammel::ConcurrentQueue is not FIFO with
        multiple producers. And in our case, we have two producer threads:
        exchangeMsgProcessingThread_ and sockMsgProcessingThread_.

        There is a race condition that could occur where `sockMsgProcessingThread_`
        enqueues an ACK, then sends the order to the exchange...the 
        exchange then sends back a OrderStatusUpdate, and exchangeMsgProcessingThread_
        then enqueues that message. However, b/c the queue isn't FIFO, here we might
        dequeue the OrderStatusUpdate BEFORE the ACK, making the Broker (who is popping)
        queue messages on the other end) think the order was Filled before even being ack'd
        by the server. 

        To robustly fix this we I think we could stamp every msg with an atomic
        sequence number, and then have a re-order buffer here, after dequeueing, to 
        make sure we send() in the right order. The atomic counter would live in
        the shared memory between the Exchange and Server.

        I don't think it's worth the effort right now, but it was something that did come up
        and definitely worth noting. The less we sleep() here, the smaller the chance of
        observing the race condition.
    */    
    while (!stopToken.stop_requested()) {
        BrokerBoundMsg msg;
        if (!brokerBoundMsgs_.try_dequeue(msg)) {
            continue;
        }
        auto& [serializedMsg, destFd] = msg;
        if (!sock_->SendMessage(std::move(serializedMsg), destFd)) {
            LOG_ERROR("Failed to send message to broker");
        }
    }
}