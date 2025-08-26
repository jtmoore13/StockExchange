#include "messages.pb.h"
#include "misc/TcpSocket/TcpSocketBase.h"


namespace ProtoUtils {
    inline proto::ExchangeNetworkMsg ExtractNetworkMsgFromSockMsg(const TcpSocketBase::SockMsg& sockMsg)
    {
        std::string serializedData(reinterpret_cast<const char*>(sockMsg.data.data()), sockMsg.size);
        proto::ExchangeNetworkMsg networkMsg;
        if (!networkMsg.ParseFromString(serializedData)) {
            throw std::runtime_error("Failed to parse NetworkMsg from socket");
        }
        return networkMsg;
    }
}
