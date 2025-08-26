#pragma once
#include "SharedTradingTypes.h"
#include "misc/Logger.h"
#include "misc/TcpSocket/TcpClientSocket.h"
#include "misc/proto/messages.pb.h"
#include <condition_variable>
#include <variant>

class Broker : LoggerBase
{
public:
    Broker(const std::string& name, const std::string& corporation);
    virtual ~Broker();

    void ConnectToExchangeServer(int exchPort, const std::string& exchIp, int reconnectTimeout);
    bool IsConnectedToExchange() const;

    bool SendMarketOrder(const std::string& underlier, TradingTypes::Qty qty, Enums::SideType sideType);
    bool SendLimitOrder(const std::string& underlier, TradingTypes::Qty qty, TradingTypes::Price price, Enums::SideType sideType);    
    bool SendCancel(int orderNum);

protected:
    virtual void Log_(const std::string& msg) const override;

    // derived classes might want to print differently, or actually do book keeping
    virtual void ProcessGatewayAck_(const proto::GatewayAck& ack);
    virtual void ProcessGatewayReject_(const proto::GatewayReject& reject);
    virtual void ProcessStatusUpdate_(const proto::OrderStatusUpdate& update);
    virtual void ProcessFill_(const proto::Fill& fill);
    virtual void ProcessCancelResponse_(const proto::CancelOrderResponse cancelResponse);

    virtual void PrintRow_(std::variant<TradingTypes::OrderId, std::string> firstCol, const std::string& symbol,
        std::string category, const std::string& details, const char* rowColor = nullptr) const;

private:
    bool SendOrder_(std::string underlier, TradingTypes::Qty qty, Enums::SideType sideType, Enums::PriceType priceType,
        std::optional<TradingTypes::Price> price = std::nullopt);

    void InitializeSocket_(int exchPort, const std::string& exchIp, int reconnectTimeout);
    void LaunchMsgProcessingThread_();
    void OnExchangeDisco_();
    void OnExchangeCxn_();

    bool SendMessage_(proto::ExchangeNetworkMsg networkMsg) const;
    void ProcessSocketMsgs_(std::stop_token stopToken);

    const std::string name_;
    const std::string corporation_;

    std::unique_ptr<TcpClientSocket> sock_; // ptr allows for lazy initilization
    std::jthread sockMsgProcessingThread_;
    mutable std::condition_variable exchangeCxnCv_;
    mutable std::mutex exchangeCvMutex_;
};