#pragma once
#include "TcpSocketBase.h"


class TcpClientSocket : public TcpSocketBase
{
public:
    enum class State {
        Uninitialized,
        Connecting,
        Connected,
        Disconnected,
        Error,
        Destroyed
    };

    TcpClientSocket(int port, const std::string& ip, int reconnectTimeoutSeconds, int id = -1);
    virtual ~TcpClientSocket();

    void ConnectToServer();
    bool IsConnected() const;
    bool SendMessage(std::vector<std::byte> msg) const;
    bool SendMessage(const std::string& msg) const;

    using OnDiscoRecoFxn = std::function<void()>;
    void SetOnCxnFxn(OnDiscoRecoFxn fxn);
    void SetOnDiscoFxn(OnDiscoRecoFxn fxn);

    State GetState() const;

private:
    virtual void ConfigureSocket_() override;
    void CloseSocket_();
    virtual void Log_(const std::string& msg) const override;

    void SetRecvTimeout_();
    void ListenForMessages_();
    void RecreateSocket_();
    void OnPeerDisconnect_();

    void SetState_(State newState, std::string_view logMsg = "");
    virtual void SetErrorStateAndThrow_(std::string logMsg) override;

    int sockId_; // just for logging purposes
    std::atomic<State> state_ = State::Uninitialized;

    static constexpr std::chrono::seconds timeBetweenRetries_ = std::chrono::seconds(1);
    static constexpr struct timeval recvTimeout_ = {0, 500}; // seconds, micros

    bool hasConnectedBefore = false;
    OnDiscoRecoFxn OnServerDisco_;
    OnDiscoRecoFxn OnServerCxn_;
    int reconnectTimeoutSeconds_ = 0;

    std::optional<std::stop_token> listeningThreadStopToken_; // for listeningThread_
};
