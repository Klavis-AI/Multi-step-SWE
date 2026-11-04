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

    ConnectionTask onTcpConnection(TcpConnectionPtr connection) override;
    void onTcpDisconnected(const TcpConnectionPtr& connection) override;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _SVR_MOD_ECHO_H_
