///////////////////////////////////////////////////////////////////////////////

#ifndef _SVR_MOD_DAYTIME_H_
#define _SVR_MOD_DAYTIME_H_

#include "ise/main/ise.h"

using namespace ise;

///////////////////////////////////////////////////////////////////////////////

class ServerModule_Daytime: public IseServerModule
{
public:
    ServerModule_Daytime() {}
    ~ServerModule_Daytime() override {}

    int getTcpServerCount() override { return 1; }
    void getTcpServerOptions(int /*serverIndex*/, TcpServerOptions& options) override { options.serverPort = 10002; }

    ConnectionTask onTcpConnection(TcpConnectionPtr connection) override;
    void onTcpDisconnected(const TcpConnectionPtr& connection) override;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _SVR_MOD_DAYTIME_H_
