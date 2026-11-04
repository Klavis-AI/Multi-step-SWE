///////////////////////////////////////////////////////////////////////////////

#ifndef _SVR_MOD_CHARGEN_H_
#define _SVR_MOD_CHARGEN_H_

#include "ise/main/ise.h"

using namespace ise;

///////////////////////////////////////////////////////////////////////////////

class ServerModule_Chargen: public IseServerModule
{
public:
    ServerModule_Chargen();
    ~ServerModule_Chargen() override {}

    int getTcpServerCount() override { return 1; }
    void getTcpServerOptions(int /*serverIndex*/, TcpServerOptions& options) override { options.serverPort = 10003; }

    void onTcpConnected(const TcpConnectionPtr& connection) override;
    void onTcpDisconnected(const TcpConnectionPtr& connection) override;
    void onTcpRecvComplete(const TcpConnectionPtr& connection,
        std::span<const std::byte> packet, const Context& context) override;
    void onTcpSendComplete(const TcpConnectionPtr& connection, const Context& context) override;

private:
    void initMessage();
private:
    string message_;
    UINT64 transferredBytes_;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _SVR_MOD_CHARGEN_H_
