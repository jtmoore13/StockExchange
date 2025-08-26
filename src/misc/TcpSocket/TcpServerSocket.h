#pragma once
#include "TcpSocketBase.h"

class TcpServerSocket : public TcpSocketBase
{
public:
    enum class State {
        Uninitialized,
        Bound,
        Listening,
        Error,
        Destroyed
    };

    TcpServerSocket(int port, const std::string& ip, int numMsgHandlingThreads);
    virtual ~TcpServerSocket() override;

    void StartListening();
    bool SendMessage(std::vector<std::byte> msg, int destFd) const;
    bool SendMessage(const std::string& msg, int destFd) const;

    using OnClientCxnFxn = std::function<void(int)>;

    // the incoming fxns will only be called from one thread,
    // so no need to make data members thread safe/atomic
    void SetOnNewCxnFxn(OnClientCxnFxn fxn);
    void SetOnClientDiscoFxn(OnClientCxnFxn fxn);

    State GetState() const;
    bool IsListening() const;

private:
    void BindSocket_();
    virtual void ConfigureSocket_() override;
    virtual void Log_(const std::string& msg) const override;

    int CreateEpollFd_();
    void UpdateEpollFd_(int fd, bool isNew);
    void HandleClientUpdate_(int fdWithUpdate);
    void HandleEpollEvents_();

    void OnNewConnection_();
    void OnPeerDisconnect_(int clientFd);
    void CloseEpoll_(void);

    void SetState_(State newState, std::string_view logMsg = "");
    virtual void SetErrorStateAndThrow_(std::string logMsg) override;
    static std::string FormatNumCxns_(int numCxns);

    OnClientCxnFxn OnNewCxn_;
    OnClientCxnFxn OnClientDisco_;

    std::atomic<State> state_ = State::Uninitialized;
    std::atomic<int> epollFd_ = -1;
    std::atomic<int> numActiveConnections_ = 0;
    
    std::mutex superBuffersMutex_;
    std::unordered_map<int, SuperBuffer> superBuffersMap_;

    std::unique_ptr<boost::asio::thread_pool> eventHandlerThreadPool_;
};
