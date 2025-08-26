#include "Exchange.h"
#include "core/SharedTradingTypes.h"
#include "core/Utils/ExchangeUtils.h"
#include "core/Utils/NetworkUtils.h"
#include "core/Utils/OrderUtils.h"
#include "misc/Colors.h"
#include "misc/DebugAssert.h"
#include "misc/proto/ProtoUtils.h"

using namespace Colors;
using namespace Enums;
using namespace TradingTypes;

namespace {

    ExchangeIPC::QueueMsg CreateStatusUpdateQueueMsg(const MatchingEngineFillEvent& fillEvent) {
        const OrderSnapshot orderSnapshot = fillEvent.snapshot;
        proto::OrderStatusUpdate update;
        update.set_order_id(orderSnapshot.orderId);
        update.set_symbol(orderSnapshot.symbol);
        update.set_status(static_cast<int32_t>(orderSnapshot.status));
        return ExchangeUtils::WrapInQueueMsg(NetworkUtils::CreateNetworkMessageWithType<proto::OrderStatusUpdate>(update));
    }

    ExchangeIPC::QueueMsg CreateFillEventQueueMsg(const MatchingEngineFillEvent& fillEvent) {
        const OrderSnapshot orderSnapshot = fillEvent.snapshot;
        proto::Fill fill;
        fill.set_qty_traded(fillEvent.qty);
        fill.set_price(fillEvent.price);
        fill.set_order_id(orderSnapshot.orderId);
        fill.set_symbol(orderSnapshot.symbol);
        fill.set_qty_remaining(orderSnapshot.qtyRemaining);
        return ExchangeUtils::WrapInQueueMsg(NetworkUtils::CreateNetworkMessageWithType<proto::Fill>(fill));
    };

    ExchangeIPC::QueueMsg CreateCancelResponseQueueMsg(OrderId orderId, bool success)
    {
        proto::CancelOrderResponse cancelResponse;
        cancelResponse.set_order_id(orderId);
        cancelResponse.set_success(success);
        return ExchangeUtils::WrapInQueueMsg(NetworkUtils::CreateNetworkMessageWithType<proto::CancelOrderResponse>(cancelResponse));
    }
}


Exchange::Exchange()
{
    InitializeSharedMemory_();
    InitializeMatchingEngines_();
    matchingEngineLookupMap_.reserve(1'000'000);
}


/*virtual*/ Exchange::~Exchange() /*override*/
{ 
    serverMsgProcessingThread_.request_stop();
    serverMsgProcessingThread_.join();

    // Clear the order books before we stop processing messages so
    // we can send the incoming 'Done' notifications (that are 
    // coming from the matching engines as they are being cleared)
    // upstream
    for (auto& [_, matchingEngine] : matchingEngines_) {
        matchingEngine->ClearBook();
    }

    rawTradeProcessingThread_.request_stop();
    rawTradeProcessingThread_.join();
}


void Exchange::ProcessServerMessages()
{
    if (!serverMsgProcessingThread_.joinable()) {
        LaunchMsgProcessingThread_();
    }
    if (!rawTradeProcessingThread_.joinable()) {
        LaunchTradeProcessingThread_();
    }
}


bool Exchange::PrintBook(const std::string& instrument)
{
    // awful and lazy yes, but only have an array
    for (const auto& supportedInstrument : shmData_->supportedInstruments) {
        if (instrument == supportedInstrument) {
            matchingEngines_[instrument]->PrintBook();
            return true;
        }
    }
    return false;
}


/*virtual*/ void Exchange::Log_(const std::string& msg) const /*override*/
{
    static const std::string prefix = "[" + LoggingPrefix::exchange + "]";
    Logger::LogMsg(msg, prefix);
}


void Exchange::InitializeMatchingEngines_()
{
    const auto& supportedInstruments = shmData_->supportedInstruments;
    const auto supportedInstrumentsSize = shmData_->supportedInstrumentsSize;

    matchingEngines_.reserve(supportedInstrumentsSize);
    for (size_t i = 0; i < supportedInstrumentsSize; ++i) {
        const std::string instrument = (supportedInstruments)[i];
        matchingEngines_[instrument] = std::make_unique<MatchingEngine>(instrument, rawTradesFromMatchingEngines_);
    }
}


void Exchange::InitializeSharedMemory_()
{  
    try {
        shmWithExchange_ = std::make_unique<SharedMemoryRegion>(ExchangeIPC::SHM_SIZE, ExchangeIPC::SHM_FILENAME, false);
    } catch (const std::exception& e) {
        std::string msg = e.what();
        if (msg.find("No such file or directory") != std::string::npos) {
            LOG_ERROR("ExchangeServer must be running before launching Exchange. Throwing...");
            throw std::runtime_error(
                "Failed to open shared memory file. This region is created by the "
                "ExchangeServer process. Is ExchangeServer running?");        
        }
        throw std::runtime_error(fmt::format("Exchange failed: {}", std::move(msg)));
    }
    shmData_ = reinterpret_cast<ExchangeIPC::Layout*>(shmWithExchange_->Get());
}


void Exchange::LaunchMsgProcessingThread_()
{
    serverMsgProcessingThread_ = std::jthread([&](std::stop_token stopToken) {
        ProcessServerMsgs_(stopToken);
    });
}


void Exchange::LaunchTradeProcessingThread_()
{
    rawTradeProcessingThread_ = std::jthread([&](std::stop_token stopToken) {
        ProcessMatchingEngineFills_(stopToken);
    });
}


void Exchange::ProcessMatchingEngineFills_(std::stop_token stopToken)
{
    LOG("Processing fill events from all matching engines...");

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        MatchingEngineFillEvent fillEvent;
        if (!rawTradesFromMatchingEngines_.try_dequeue(fillEvent)) [[unlikely]] {
            if (stopToken.stop_requested()) [[unlikely]] { // we do this here to ensure the queue is emptied
                return;
            } 
            continue; // empty queue
        }

        // checking > 0 b/c MatchingEngineFillEvents include market orders that go unfilled
        if (fillEvent.qty > 0) {
            shmData_->exchangeToServerQueue.Enqueue(CreateFillEventQueueMsg(fillEvent));
        }
        if (fillEvent.orderStatusChanged) {
            shmData_->exchangeToServerQueue.Enqueue(CreateStatusUpdateQueueMsg(fillEvent));
        }
    }
}


std::unique_ptr<ExchangeOrder> Exchange::CreateExchangeOrder_(const proto::OrderRequest& orderRequest)
{
    const std::string& symbol = orderRequest.symbol();
    const std::string& instrument = orderRequest.instrument();
    const Qty qty = orderRequest.qty();
    const SideType side = static_cast<SideType>(orderRequest.side_type());
    const PriceType priceType = static_cast<PriceType>(orderRequest.price_type());
    const OrderId orderId = orderRequest.order_id();

    std::unique_ptr<ExchangeOrder> order;
    if (priceType == PriceType::LIMIT) {
        order = ExchangeOrder::MakeLimitOrder(symbol, instrument, qty, orderRequest.price(), side, orderId);
    } else if (priceType == PriceType::MARKET) {
        order = ExchangeOrder::MakeMarketOrder(symbol, instrument, qty, side, orderId);
    } else {
        LOG_ERROR("Invalid price type. Failed to create order");
        return nullptr;
    }
    return order;
}


ExchangeIPC::QueueMsg Exchange::CreateSerializedOrderUpdate_(const ExchangeOrder& order)
{
    proto::OrderStatusUpdate update;
    update.set_order_id(order.orderId_);
    update.set_status(static_cast<int32_t>(order.status_));
    update.set_symbol(order.symbol_);

    // We need to wrap every queue message in a fixed-size element. That
    // way, we can allocate the shared queue on the stack instead of the heap.
    return ExchangeUtils::WrapInQueueMsg(NetworkUtils::CreateNetworkMessageWithType<proto::OrderStatusUpdate>(update));
}


void Exchange::ProcessServerMsgs_(std::stop_token stopToken)
{
    LOG("Polling for server messages...");

    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        ExchangeIPC::QueueMsg serializedNetworkMsg;
        if (shmData_->serverToExchangeQueue.Dequeue(serializedNetworkMsg)) {

            proto::ExchangeNetworkMsg networkMsg;
            networkMsg.ParseFromArray(serializedNetworkMsg.msg.data(), serializedNetworkMsg.msgSize);

            // server should only be sending order request and cancels
            DEBUG_ASSERT(networkMsg.has_cancel_order() || networkMsg.has_order_request());

            if (networkMsg.has_order_request()) {
                HandleIncomingOrderRequest_(*networkMsg.mutable_order_request());
            } 
            else if (networkMsg.has_cancel_order()) {
                HandleIncomingCancelOrder_(networkMsg.cancel_order());
            }
        }

        if (!shmData_->isOpen.load()) {
            // server is closed and no orders to process, no reason to burn CPU
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}


void Exchange::GiveToMatchingEngine_(std::unique_ptr<ExchangeOrder> order)
{
    const auto& matchingEngine = matchingEngines_.at(order->instrument_);
    const OrderId orderId = order->orderId_; // save before moving
    if (!matchingEngine->AddOrder(std::move(order))) {
        LOG_ERROR("Failed to insert {}" + order->readableOrderId_ + " into order book.");
    }
    matchingEngineLookupMap_[orderId] = &(*matchingEngine);
}


void Exchange::UpdateOrderStatus_(ExchangeOrder& order, OrderStatus newStatus)
{
    if (order.status_ == newStatus) {
        return;
    }
    order.status_ = newStatus;
    shmData_->exchangeToServerQueue.Enqueue(CreateSerializedOrderUpdate_(order));
}


void Exchange::HandleIncomingOrderRequest_(proto::OrderRequest& orderRequest)
{
    /*
        Offically create the order object here. The MatchingEngines will be 
        the ones that owns and update the orders, no one else. This keeps things
        fast and simple.

        Whenever something important happens (like a status update or trade), we will
        take a snapshot of the order and send it to other parts of the system. This
        should keep the matching engines simple and safe, and lets everything else
        stay in sync without slowing it down.
    */

    std::unique_ptr<ExchangeOrder> order = CreateExchangeOrder_(orderRequest);
    if (!order) { 
        return;
    }

    LOG_VERBOSE(fmt::format("Created order! {} | {}", OrderUtils::GetLoggedOrderId(order->orderId_), order->symbol_));
    UpdateOrderStatus_(*order, OrderStatus::Accepted);
    GiveToMatchingEngine_(std::move(order));
}


void Exchange::HandleIncomingCancelOrder_(const proto::CancelOrder& cancelOrder)
{
    // find the matching engine that owns the order ID
    const auto orderId = OrderUtils::GenerateOrderId(cancelOrder.session_id(), cancelOrder.order_num());
    const auto it = matchingEngineLookupMap_.find(orderId);
    if (it == matchingEngineLookupMap_.end()) {
        LOG_WARNING(fmt::format("Failed to cancel order {}; OrderId not mapped to a MatchingEngine",
            OrderUtils::GetReadableOrderId(orderId)));
        return;
    }

    // tell the matching engine to cancel the order!
    MatchingEngine& matchingEngine = *it->second;
    const bool success = matchingEngine.CancelOrder(orderId);
    shmData_->exchangeToServerQueue.Enqueue(CreateCancelResponseQueueMsg(orderId, success));
}