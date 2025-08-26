#include "TcpServerSocket.h"
#include "Colors.h"

using namespace Colors;

namespace {

    std::string GetStateStr(TcpServerSocket::State state)
    {
        static const std::unordered_map<TcpServerSocket::State, std::string> colors {
            { TcpServerSocket::State::Destroyed, ColorUtils::Wrap("Destroyed", Grey) },
            { TcpServerSocket::State::Bound,     ColorUtils::Wrap("Bound", Yellow) },
            { TcpServerSocket::State::Listening, ColorUtils::Wrap("Listening", Green) },
            { TcpServerSocket::State::Error,     ColorUtils::Wrap("Error", Red) },
        };
        auto it = colors.find(state);
        if (it != colors.end()) {
            return it->second;
        }
        return "Unknown";
    }
}


TcpServerSocket::TcpServerSocket(int port, const std::string& ip, int numMsgHandlerThreads) :
   TcpSocketBase(port, ip)
{
    eventHandlerThreads_ = std::make_unique<boost::asio::thread_pool>(numMsgHandlerThreads);
    ConfigureSocket_();
    BindSocket_();
}


/*virtual*/ TcpServerSocket::~TcpServerSocket() /*override*/
{
    CloseEpoll_();
    eventHandlerThreads_->join();
    SetState_(State::Destroyed);
}


void TcpServerSocket::StartListening()
{
    if (listen(sockFd_.load(), SOMAXCONN) == -1) {
        SetErrorStateAndThrow_("Failed to start listening");
    }
    SetState_(State::Listening);
    epollFd_.store(CreateEpollFd_());
    UpdateEpollFd_(sockFd_.load(), true); // add the listening socket so we can accept new connections
    listeningThread_ = std::jthread(&TcpServerSocket::HandleEpollEvents_, this);
}


bool TcpServerSocket::SendMessage(std::vector<std::byte> msg, int destFd) const
{
    return SendMessageImpl_(msg, destFd);
}


bool TcpServerSocket::SendMessage(const std::string& msg, int destFd) const
{
    return SendMessageImpl_(StringToBytesVec_(msg), destFd);
}


TcpServerSocket::State TcpServerSocket::GetState() const
{
    return state_.load();
}


bool TcpServerSocket::IsListening() const
{
    return state_.load() == State::Listening;
}


void TcpServerSocket::SetOnNewCxnFxn(OnClientCxnFxn fxn)
{
    OnNewCxn_ = std::move(fxn);
}


void TcpServerSocket::SetOnClientDiscoFxn(OnClientCxnFxn fxn)
{
    OnClientDisco_ = std::move(fxn);
}


void TcpServerSocket::BindSocket_()
{
    if (bind(sockFd_.load(), (sockaddr*)&serverAddr_, sizeof(serverAddr_)) == -1) {
        SetErrorStateAndThrow_("Failed to bind to IP/Port");
    }
    LOG(fmt::format("Socket binded to port {}.", serverPort_));
}


void TcpServerSocket::ConfigureSocket_()
{
    MakeNonBlocking_();
}


/*virtual*/ void TcpServerSocket::Log_(const std::string& msg) const /*override*/
{
    static const std::string prefix = "[" + LoggingPrefix::serverSocket + "]";
    Logger::LogMsg(msg, prefix);
}


int TcpServerSocket::CreateEpollFd_()
{
    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        SetErrorStateAndThrow_("Failed to create epoll instance");
    }
    return epollFd;
}


void TcpServerSocket::UpdateEpollFd_(int fd, bool isNew)
{
    /*
        Adds or re-arms an FD in epoll with EPOLLONESHOT.
        EPOLLONESHOT ensures an FD is reported only once by epoll_wait().
        After handling the event, the FD must be explicitly re-armed,
        or it will never generate another event.
    */
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLONESHOT;
    event.data.fd = fd;
    int mode = isNew ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    if (epoll_ctl(epollFd_.load(), mode, fd, &event) == -1) {
        SetErrorStateAndThrow_("epoll_ctl failed for " + std::string(isNew ? "ADD" : "MOD"));
    }
}


void TcpServerSocket::HandleClientUpdate_(int clientFd)
{
    static constexpr int maxRecvCalls = 10;
    std::byte recvBuffer[recvBufferSize_] = {};

    int recvCalls = 0;
    while (++recvCalls <= maxRecvCalls) {
        const int bytesReceived = recv(clientFd, recvBuffer, recvBufferSize_, 0);
        if (bytesReceived <= 0) {
            if (bytesReceived == 0) {
                OnPeerDisconnect_(clientFd);
                return;
            }
            // could be more verbose about when to break but this is fine for now
            if (IsClosed_()) {
                break;
            }
            SetErrorStateAndThrow_("Unexpected error with recv(): " + std::string(strerror(errno)));

        }
        std::lock_guard lock(superBuffersMutex_);
        SuperBuffer& superBuffer = superBuffersMap_[clientFd];
        superBuffer.insert(superBuffer.end(), recvBuffer, recvBuffer + bytesReceived);
        ParseSuperBuffer_(superBuffer, clientFd);
        if (superBuffer.empty()) {
            break;
        }
    }
    UpdateEpollFd_(clientFd, false);
}


void TcpServerSocket::HandleEpollEvents_()
{
    // this function gets executed by listeningThread_

    static constexpr int MAX_EVENTS = 512;
    static constexpr int TIMEOUT = 1000; // ms
    static epoll_event events[MAX_EVENTS];

    while (true) {
        int epoll = epollFd_.load();
        if (epoll == -1) {
            break;
        }
        // waits for events, but 1000 ms timeout in case epollFd_ was changed
        const int numEvents = epoll_wait(epoll, events, MAX_EVENTS, TIMEOUT);
        if (numEvents == -1) {
            SetErrorStateAndThrow_(fmt::format("unexpected epoll_wait() failed: {}", strerror(errno)));
        }
        for (int i = 0; i < numEvents; ++i) {
            const int fdWithUpdate = events[i].data.fd;
            if (fdWithUpdate == sockFd_.load()) {
                OnNewConnection_();
                continue;
            }
            boost::asio::post(*eventHandlerThreads_, [this, fdWithUpdate]() {
                try {
                    HandleClientUpdate_(fdWithUpdate);
                } catch (const std::exception& e) {
                    LOG_ERROR(e.what());
                }
            });
        }
    }
}


void TcpServerSocket::OnNewConnection_()
{
    [[maybe_unused]] int numCxns = ++numActiveConnections_; // fetch_add(1) + 1

    sockaddr_in clientAddr;
    socklen_t clientSize = sizeof(clientAddr);
    const int sockFd = sockFd_.load();

    const int newClientFd = accept(sockFd, (sockaddr*)&clientAddr, &clientSize);
    if (newClientFd == -1) {
        LOG("Failed to accept new connection.");
        return;
    }
    // start listening for new client updates
    UpdateEpollFd_(newClientFd, true);
    {
        std::lock_guard lock(superBuffersMutex_);
        SuperBuffer& buffer = superBuffersMap_[newClientFd]; // default constructs
        buffer.reserve(recvBufferSize_ * 2);
    }
    // re-arm the listening socket
    UpdateEpollFd_(sockFd, false);

    LOG(fmt::format("New connection accepted! {}", FormatNumCxns_(numCxns)));
    if (OnNewCxn_) {
        OnNewCxn_(newClientFd);
    }
}


void TcpServerSocket::OnPeerDisconnect_(int clientFd)
{
    [[maybe_unused]] int numCxns = --numActiveConnections_; // fetch_sub(1) - 1
    {
        std::lock_guard lock(superBuffersMutex_);
        superBuffersMap_.erase(clientFd);
    }
    if (epoll_ctl(epollFd_.load(), EPOLL_CTL_DEL, clientFd, nullptr) == -1) {
        SetErrorStateAndThrow_("Failed to remove fd from epoll");
        return;
    }
    LOG(fmt::format("Client disconnected {}", FormatNumCxns_(numCxns)));
    if (OnClientDisco_) {
        OnClientDisco_(clientFd);
    }
}


void TcpServerSocket::CloseEpoll_()
{
    // set to -1 for clarity, but still want to close the old fd
    int fd = epollFd_.exchange(-1);
    if (close(fd) == -1) {
        SetErrorStateAndThrow_("Failed to close epollFd_");
    }
}


void TcpServerSocket::SetState_(State newState, std::string_view logMsg)
{
    if (newState == state_.load()) {
        return;
    }
    state_.store(newState);

    /*
        This LOG gets called from both destructors which is technically safe right
        now, but wouldn't be if someone inherited from either derived class
        and didn't implement their own Log_() fxn. Fine for this project, but 
        good to note. Should probably just change the way we log to avoid inheritance
        down the road.
    */
    LOG(fmt::format("{}{}", GetStateStr(newState), logMsg.empty() ? "" : fmt::format(": {}", logMsg)));
}


/*virtual*/ void TcpServerSocket::SetErrorStateAndThrow_(std::string logMsg) /*override*/
{
    SetState_(State::Error);
    throw std::runtime_error(std::move(logMsg));
}


/*static*/ std::string TcpServerSocket::FormatNumCxns_(int numCxns)
{
    std::ostringstream oss;
    oss << "(now " << numCxns << " active connection" << (numCxns == 1 ? ")" : "s)");
    return oss.str();
}
