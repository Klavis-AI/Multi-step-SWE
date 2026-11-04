///////////////////////////////////////////////////////////////////////////////

#ifndef _SVR_MOD_ECHO_H_
#define _SVR_MOD_ECHO_H_

#include "ise/main/ise.h"

using namespace ise;

///////////////////////////////////////////////////////////////////////////////

class ServerModule_Echo: public IseServerModule
{
public:
    ServerModule_Echo() {}
    ~ServerModule_Echo() override {}

    int getTcpServerCount() override { return 1; }
    void getTcpServerOptions(int /*serverIndex*/, TcpServerOptions& options) override { options.serverPort = 10000; }

    void onTcpConnected(const TcpConnectionPtr& connection) override;
    void onTcpDisconnected(const TcpConnectionPtr& connection) override;
    void onTcpRecvComplete(const TcpConnectionPtr& connection,
        std::span<const std::byte> packet, const Context& context) override;
    void onTcpSendComplete(const TcpConnectionPtr& connection, const Context& context) override;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _SVR_MOD_ECHO_H_
