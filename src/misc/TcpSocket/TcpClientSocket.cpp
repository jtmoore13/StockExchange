#include "TcpClientSocket.h"
#include "fmt/core.h"
#include "Colors.h"

using namespace Colors;

namespace {
    std::string GetStateStr(TcpClientSocket::State state)
    {
        static const std::unordered_map<TcpClientSocket::State, std::string> colors {
            { TcpClientSocket::State::Connecting,    ColorUtils::Wrap("Connecting", Yellow) },
            { TcpClientSocket::State::Connected,     ColorUtils::Wrap("Connected",  Green) },
            { TcpClientSocket::State::Disconnected,  ColorUtils::Wrap("Disconnected", Red) },
            { TcpClientSocket::State::Error,         ColorUtils::Wrap("Error",       Red) },
            { TcpClientSocket::State::Destroyed,     ColorUtils::Wrap("Destroyed",   Grey) },
        };
        auto it = colors.find(state);
        if (it != colors.end()) {
            return it->second;
        }
        return "Unknown";
    }
}


TcpClientSocket::TcpClientSocket(int port, const std::string& ip, int reconnectTimeoutSeconds, int id) :
    TcpSocketBase(port, ip),
    sockId_(id),
    reconnectTimeoutSeconds_(reconnectTimeoutSeconds)
{
    ConfigureSocket_();
}


/*virtual*/ TcpClientSocket::~TcpClientSocket() /*override*/
{
    if (listeningThread_.joinable()) {
        listeningThread_.request_stop();
        listeningThread_.join();
    }
    CloseSocket_();
    SetState_(State::Destroyed);
}


void TcpClientSocket::ConnectToServer()
{
    const int maxRetries = reconnectTimeoutSeconds_ / timeBetweenRetries_.count();
    SetState_(State::Connecting, fmt::format("attempting to {} to server at {}:{}...", 
        hasConnectedBefore ? "reconnect" : "connect", serverIp_, serverPort_));
    
    for (int i = 1; i <= maxRetries; ++i) {
        // in case the socket is going out of scope and we want to quit immediately
        if (listeningThreadStopToken_ && listeningThreadStopToken_.value().stop_requested()) { 
            break;
        }
        if (connect(sockFd_.load(), (sockaddr*)&serverAddr_, sizeof(serverAddr_)) == 0) {
            SetState_(State::Connected);
            if (!hasConnectedBefore) {
                hasConnectedBefore = true;
            } 
            if (OnServerCxn_) {
                OnServerCxn_();
            }
            break;
        };
        if (i == maxRetries) {
            SetState_(State::Disconnected, "connection timeout expired.");
            return;
        }
        // after connect() fails, the socket it was called on might enter a 
        // bad or undefined state so we have to recreate it before retrying
        RecreateSocket_();
        std::this_thread::sleep_for(timeBetweenRetries_);
    }

    // launch the listening thread if we have not already
    if (IsConnected() && !listeningThread_.joinable()) {
        LOG("Launching thread to listen for server messages.");
        listeningThread_ = std::jthread([&](){
            // jthread doesn't create the stop_token until the thread is constructed,
            // so have to initialize here
            listeningThreadStopToken_ = listeningThread_.get_stop_token();
            ListenForMessages_();
        });
    }
}


bool TcpClientSocket::IsConnected() const
{
    return state_.load() == State::Connected;
}


bool TcpClientSocket::SendMessage(std::vector<std::byte> msg) const
{
    return SendMessageImpl_(msg, sockFd_.load());
}


bool TcpClientSocket::SendMessage(const std::string& msg) const
{
    return SendMessage(StringToBytesVec_(msg));
}


void TcpClientSocket::SetOnCxnFxn(OnDiscoRecoFxn fxn)
{
    OnServerCxn_ = std::move(fxn);
}


void TcpClientSocket::SetOnDiscoFxn(OnDiscoRecoFxn fxn)
{
    OnServerDisco_ = std::move(fxn);
}


TcpClientSocket::State TcpClientSocket::GetState() const
{
    return state_.load();
}


/*virtual*/ void TcpClientSocket::ConfigureSocket_() /*override*/
{
    SetRecvTimeout_();
}


void TcpClientSocket::CloseSocket_()
{
    /*
        Let other threads know that sockFd_ is about to be
        closed by setting it to -1, but still need to save 
        the old value so we can properly call close() on it
    */
    int fd = sockFd_.exchange(-1);
    if (fd == -1) {
        return;
    }
    if (close(fd) == -1) {
        SetErrorStateAndThrow_("Failed to close socket with fd " + std::to_string(fd));
    }
}


/*virtual*/ void TcpClientSocket::Log_(const std::string& msg) const
{
    if (sockId_ == -1) {
        static const std::string prefix = "[" + LoggingPrefix::clientSocket + "]";
        Logger::LogMsg(msg, prefix);
    } else {
        Logger::LogMsg(msg, fmt::format("[{} {}]", LoggingPrefix::clientSocket, sockId_));
    }
}


void TcpClientSocket::SetRecvTimeout_(void)
{
    if (setsockopt(sockFd_.load(), SOL_SOCKET, SO_RCVTIMEO, (const char *)&recvTimeout_, sizeof(recvTimeout_)) < 0) {
        SetErrorStateAndThrow_("Failed to set recv timeout");
    }
}


void TcpClientSocket::ListenForMessages_()
{
    /*
        Unlike the the server socket, this function will only be called once,
        on its own thread. So we can just use one buffer (vs. maintaining a
        thread-safe map of them like we do in the server)
    */
    SuperBuffer superBuffer = {};
    std::byte recvBuffer[recvBufferSize_] = {};

    const std::stop_token stopToken = listeningThreadStopToken_.value(); // we know it will have a value at this point

    while (!stopToken.stop_requested()) {
        const int sockFd = sockFd_.load();
        const int bytesReceived = recv(sockFd, recvBuffer, recvBufferSize_, 0); // blocks, but only for recvTimeout_
        if (bytesReceived <= 0) {
            if (bytesReceived == 0) {
                OnPeerDisconnect_();
                continue;
            }
            int err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) { // expected blocking timeout
                continue;
            }
            if (IsClosed_() || err == ECONNRESET || err == ENOTCONN || err == EBADF) {
                break;
            }
            SetErrorStateAndThrow_("Unexpected error with recv(): " + std::string(strerror(err)));
        }
        superBuffer.insert(superBuffer.end(), recvBuffer, recvBuffer + bytesReceived);
        ParseSuperBuffer_(superBuffer, sockFd);
   }
}


void TcpClientSocket::RecreateSocket_()
{
    CloseSocket_();
    sockFd_.store(CreateSocket_());
    ConfigureSocket_();
}


void TcpClientSocket::OnPeerDisconnect_()
{
    SetState_(State::Disconnected, "server disconnected.");
    if (OnServerDisco_) {
        OnServerDisco_();
    }
    ConnectToServer();
}


void TcpClientSocket::SetState_(State newState, std::string_view logMsg)
{
    if (state_.load() == newState) {
        return;
    }
    state_.store(newState);
    std::string msg = GetStateStr(newState);
    if (!logMsg.empty()) {
        msg += ": ";
        msg += logMsg;
    }
    LOG(std::move(msg));
}


/*virtual*/ void TcpClientSocket::SetErrorStateAndThrow_(std::string logMsg) /*override*/
{
    SetState_(State::Error);
    throw std::runtime_error(std::move(logMsg));
}