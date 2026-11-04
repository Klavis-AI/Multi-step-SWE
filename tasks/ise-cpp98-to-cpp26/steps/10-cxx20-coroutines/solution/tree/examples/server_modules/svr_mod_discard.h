///////////////////////////////////////////////////////////////////////////////

#ifndef _SVR_MOD_DISCARD_H_
#define _SVR_MOD_DISCARD_H_

#include "ise/main/ise.h"

using namespace ise;

///////////////////////////////////////////////////////////////////////////////

class ServerModule_Discard: public IseServerModule
{
public:
    ServerModule_Discard() {}
    ~ServerModule_Discard() override {}

    int getTcpServerCount() override { return 1; }
    void getTcpServerOptions(int /*serverIndex*/, TcpServerOptions& options) override { options.serverPort = 10001; }

    ConnectionTask onTcpConnection(TcpConnectionPtr connection) override;
    void onTcpDisconnected(const TcpConnectionPtr& connection) override;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _SVR_MOD_DISCARD_H_
