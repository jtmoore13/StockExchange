#pragma once
#include "SharedTradingTypes.h"
#include "ExchangeConfig.h"
#include "misc/Logger.h"
#include "misc/SharedMemoryRegion.h"
#include "misc/proto/messages.pb.h"
#include <boost/bimap.hpp>

class TcpServerSocket;

class ExchangeServer : LoggerBase
{
public:

    ExchangeServer(const std::unordered_set<std::string>& supportedInstruments);
    virtual ~ExchangeServer();

    void StartListening(int port, const std::string& ip);
    void SetOpen(bool isOpen);
    bool IsOpen() const;

    // let user decide what to do with the shared memory region when it's destroyed
    void EnableSharedMemoryCleanupOnShutdown();

private:

    virtual void Log_(const std::string& msg) const override;

    void InitializeSharedMemory_();
    void InitializeSocket_(int port, const std::string& ip);

    void LaunchSendMsgsToBrokersThread_();
    void SendMsgsToBroker_(std::stop_token stopToken);

    /*
        These fxns are for enqueueing msgs that will be sent back to brokers. 
        A dedicated thread (brokerSendingMsgThread_) continuously processes
        queue msgs and send each message to the appropriate destination.
    */
    void EnqueueNetworkMsg_(const proto::ExchangeNetworkMsg& networkMsg, int destFd);
    void EnqueueGatewayRejectNotification_(const proto::OrderRequest& rejectedRequest, std::string error, int destFd);
    void EnqueueGatewayAckNotification_(const proto::OrderRequest& acceptedRequest, TradingTypes::OrderId orderId);
    void EnqueueOrderStatusUpdateNotification_(proto::ExchangeNetworkMsg networkMsg);
    void EnqueueFillNotification_(proto::ExchangeNetworkMsg networkMsg);
    void EnqueueCancelOrderResponseNotification_(proto::ExchangeNetworkMsg networkMsg);

    void AddToExchangeQueue_(proto::ExchangeNetworkMsg networkMsg);

    void OnNewCxn_(int newCxnFd);
    void OnBrokerDiscxn_(int disconnectingCxnFd);

    std::optional<TradingTypes::SessionId> LookupSessionId_(int cxnFd) const;
    using DestinationFd = int;
    std::optional<DestinationFd> GetDestinationFd_(TradingTypes::OrderId orderId) const;

    void LaunchExchMsgProcessingThread_();
    void ProcessExchangeMsgs_(std::stop_token stopToken);

    std::string ValidateOrderData_(const proto::OrderRequest& order) const;

    void LaunchSockMsgProcessingThread_();
    void ProcessIncomingSockMsgs_(std::stop_token stopToken);

    const std::unordered_set<std::string> supportedInstruments_;
    unsigned allTimeCxns_ = 0; 

    std::unique_ptr<TcpServerSocket> sock_; // manages connections with clients (brokers)

    SharedMemoryRegion shrMem_;
    ExchangeIPC::Layout* shmWithExchange_ = nullptr;

    std::jthread sockMsgProcessingThread_;
    std::jthread exchangeMsgProcessingThread_;
    std::jthread sendMsgsToBrokersThread_;

    using SerializedNetworkMsg = std::string;
    using BrokerBoundMsg = std::pair<SerializedNetworkMsg, DestinationFd>;
    moodycamel::ConcurrentQueue<BrokerBoundMsg> brokerBoundMsgs_;

    /*
        This maps SessionId to the file descriptor that that the connection
        is currently using. We need to keep track of this b/c if
        a broker disconnects, and a new broker connects and is assigned
        the old broker's fd, updates from the Exchange will think they
        should go to the new broker's fd. And obviously, the those orders don't
        belong to the new broker.
        
        In the case a Broker sends a resting order, and then disconnects,
        we should figure out what to do. But this mitigates inaccurate
        notifications for now.
    */
    using CxnFd = int;
    boost::bimap<TradingTypes::SessionId, CxnFd> sessionIdsMap_;
    mutable std::mutex sessionsMutex_;
};