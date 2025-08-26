#include "TcpSocketBase.h"
#include "Logger.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "fmt/core.h"
#include <iostream>


TcpSocketBase::TcpSocketBase(int serverPort, const std::string& serverIp) :
    serverPort_(serverPort),
    serverIp_(serverIp),
    sockFd_(CreateSocket_())
{
    SetServerAddress_();
}


/*virtual*/ TcpSocketBase::~TcpSocketBase()
{ 
    close(sockFd_);
}

// TODO : CHECK OUT SHARED MEMORY REGION, MAYBE BUG THERE?

TcpSocketBase::SockMsg::SockMsg(const std::byte* src, size_t size, int cxnFd, int seqNum) :
   size(size),
   cxnFd(cxnFd),
   seqNum(seqNum)
{
    if (src && size > 0) {
        memcpy(data.data(), src, size);
    }
}


std::optional<TcpSocketBase::SockMsg> TcpSocketBase::GetNextMessage()
{
    SockMsg msg;
    if (msgQueue_.try_dequeue(msg)) {
        return msg;
    }
    return std::nullopt;
}


void TcpSocketBase::Close()
{
    close(sockFd_);
}


int TcpSocketBase::CreateSocket_()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        SetErrorStateAndThrow_("Failed to create TcpSocket");
    }
    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));  // allow socket to be reused
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));  // disable Nagle's algorithm for lower latency
    return fd;
}


void TcpSocketBase::SetServerAddress_()
{
    serverAddr_.sin_family = AF_INET;
    // htons converts int to network-byte-order, which is Big Endian.
    // Always convert to NBO to stay compatible on different architectures.
    serverAddr_.sin_port = htons(serverPort_);
    if (inet_pton(AF_INET, serverIp_.c_str(), &serverAddr_.sin_addr) <= 0) {
        SetErrorStateAndThrow_("Invalid IP address.");
    };
}


void TcpSocketBase::MakeNonBlocking_()
{
    const int sockFd = sockFd_.load();
    int flags = fcntl(sockFd, F_GETFL, 0);
    if (flags == -1) {
        SetErrorStateAndThrow_("failed to set flags");
    }
    if (fcntl(sockFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        SetErrorStateAndThrow_("failed to make socket nonblocking");
    }
}


bool TcpSocketBase::SendMessageImpl_(const std::vector<std::byte>& msg, int destFd) const
{
    const size_t msgSize = msg.size();
    if (msgSize > maxMsgSize) {
        LOG_ERROR("Failed to send: msg too big");
        return false;
    }

    const size_t bytesToSend = msgSize + headerSize_;

    // fist two bytes are the payload size, rest is the payload
    std::vector<char> msgBuffer(bytesToSend);
    memcpy(msgBuffer.data(), &msgSize, headerSize_);
    memcpy(msgBuffer.data() + headerSize_, msg.data(), msgSize);

    unsigned totalBytesSent = 0;
    // loop until all bytes are sent, as send() may send only part of the data
    while (totalBytesSent < bytesToSend) {
        const auto bytesSent = send(destFd, msgBuffer.data() + totalBytesSent, bytesToSend - totalBytesSent, 0);
        if (bytesSent == -1) {
            LOG_ERROR(fmt::format("Error sending message: {}", strerror(errno)));
            return false;
        }
        totalBytesSent += bytesSent;
    }
    return true;
}


void TcpSocketBase::ParseSuperBuffer_(SuperBuffer& superBuffer, int cxnFd)
{
    unsigned msgStartPos = 0;
    while (msgStartPos + headerSize_ < superBuffer.size()) {
        uint16_t msgSize;
        memcpy(&msgSize, superBuffer.data() + msgStartPos, headerSize_);
        if (msgStartPos + headerSize_ + msgSize > superBuffer.size()) {
            break;  // sender's entire message couldn't fit in the buffer
        }
    
        // we have a full message! push it into the queue
        const std::byte* msgStart = superBuffer.data() + msgStartPos + headerSize_;
        int seqNum = totalMsgCount_.fetch_add(1);
        if (!msgQueue_.try_enqueue(SockMsg(msgStart, msgSize, cxnFd, seqNum))) {
            LOG_WARNING("QUEUE FULL, MESSAGE DROPPED");
            break;
        }
        msgStartPos += headerSize_ + msgSize;
    }

    // move the leftover partial message (if any) to the front, and resize
    const int numLeftoverChars = superBuffer.size() - msgStartPos;
    if (numLeftoverChars > 0) {
        std::memmove(superBuffer.data(), superBuffer.data() + msgStartPos, numLeftoverChars);
    }
    superBuffer.resize(numLeftoverChars);
}


bool TcpSocketBase::IsClosed_() const
{
    return sockFd_ == -1;
}


/*static*/ std::vector<std::byte> TcpSocketBase::StringToBytesVec_(const std::string& str)
{
    return std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(str.data()),
        reinterpret_cast<const std::byte*>(str.data() + str.size())
    );
}
