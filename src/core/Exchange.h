#pragma once
#include "ExchangeConfig.h"
#include "ExchangeOrder.h"
#include "MatchingEngine.h"
#include "misc/Logger.h"
#include "misc/SharedMemoryRegion.h"

namespace proto {
    class OrderRequest;
    class CancelOrder;
}

class Exchange : public LoggerBase
{
public:
    Exchange();
    virtual ~Exchange();

    void ProcessServerMessages();

    bool PrintBook(const std::string& instrument); // for ExchangeMain.cpp

private:
    virtual void Log_(const std::string& msg) const override;

    void ProcessServerMsgs_(std::stop_token stopToken);
    void LaunchMsgProcessingThread_();

    void ProcessMatchingEngineFills_(std::stop_token stopToken);
    void LaunchTradeProcessingThread_();

    void InitializeMatchingEngines_();
    void InitializeSharedMemory_();
    void GiveToMatchingEngine_(std::unique_ptr<ExchangeOrder> order);

    std::unique_ptr<ExchangeOrder> CreateExchangeOrder_(const proto::OrderRequest& orderRequest);

    ExchangeIPC::QueueMsg CreateSerializedOrderUpdate_(const ExchangeOrder& order);
    ExchangeIPC::QueueMsg CreateSerializedOrderUpdate_(const TradingTypes::MatchingEngineFillEventsQueue& fillEvent);

    void UpdateOrderStatus_(ExchangeOrder& order, Enums::OrderStatus newStatus);

    // these events are received from the Server
    void HandleIncomingOrderRequest_(proto::OrderRequest& orderRequest);
    void HandleIncomingCancelOrder_(const proto::CancelOrder& cancelOrder);

    std::unique_ptr<SharedMemoryRegion> shmWithExchange_;
    ExchangeIPC::Layout* shmData_; // the actual struct of data structures within the shared memory region

    TradingTypes::MatchingEngineFillEventsQueue rawTradesFromMatchingEngines_;
    std::unordered_map<TradingTypes::Instrument, std::unique_ptr<MatchingEngine>> matchingEngines_;
    std::jthread rawTradeProcessingThread_;
    std::jthread serverMsgProcessingThread_;

    // keeps track of what order is owned by what matching engine
    std::unordered_map<TradingTypes::OrderId, MatchingEngine*> matchingEngineLookupMap_;
};