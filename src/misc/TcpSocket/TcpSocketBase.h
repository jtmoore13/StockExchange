#pragma once
#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <cstring>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include "libraries/concurrentqueue.h"
#include "Logger.h"


class TcpSocketBase : public LoggerBase
{
public:

    static constexpr unsigned maxMsgSize = 1024;

    struct SockMsg {
        SockMsg() = default;
        SockMsg(const std::byte* src, size_t dataSize, int fd, int seqNum);
        SockMsg(const SockMsg&) = default;
        SockMsg(SockMsg&&) noexcept = default;
        SockMsg& operator=(const SockMsg&) = default;

        size_t size;
        std::array<std::byte, maxMsgSize> data;
        int cxnFd;
        int seqNum;
    };

    TcpSocketBase(int serverPort, const std::string& serverIp);
    virtual ~TcpSocketBase() = 0;

    std::optional<SockMsg> GetNextMessage();
    void Close();

protected:
    int CreateSocket_();
    void SetServerAddress_();
    void MakeNonBlocking_();

    virtual void ConfigureSocket_() = 0;
    virtual void SetErrorStateAndThrow_(std::string logMsg) = 0;

    /*
        We should really use a ring buffer here instead of a std::vector so
        we don't have to move leftover contents from the back to the front.
        The head/tail pointers would just automatically update, but too lazy
        for that right now.
    */
    using SuperBuffer = std::vector<std::byte>;
    void ParseSuperBuffer_(SuperBuffer& superBuffer, int cxnFd);
    bool SendMessageImpl_(const std::vector<std::byte>& msg, int destFd) const;

    bool IsClosed_() const;

    static std::vector<std::byte> StringToBytesVec_(const std::string& str);

    const int serverPort_;
    const std::string serverIp_;
    std::atomic<int> sockFd_; // atomic b/c we set it to -1 when closed (by some thread)
    sockaddr_in serverAddr_;

    static constexpr unsigned recvBufferSize_ = 4096;
    static constexpr uint16_t headerSize_ = 2;
    static_assert(headerSize_ == 2, "msg headerSize_ must be 2");
    static_assert(maxMsgSize < recvBufferSize_, "invalid recvBufferSize_");

    // ideally this would be on its own cache line so it doesn't interfere with other variables
    alignas(64) std::atomic<unsigned> totalMsgCount_ = 0;

    moodycamel::ConcurrentQueue<SockMsg> msgQueue_; // MPMC but we only use one consumer

    std::jthread listeningThread_;
};
