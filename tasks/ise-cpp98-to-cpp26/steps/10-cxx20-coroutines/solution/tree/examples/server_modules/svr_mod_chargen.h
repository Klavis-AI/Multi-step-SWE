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

    ConnectionTask onTcpConnection(TcpConnectionPtr connection) override;
    void onTcpDisconnected(const TcpConnectionPtr& connection) override;

private:
    void initMessage();
    // 一条连接上的第二个会话协程: 只管把对方发来的东西收下来扔掉
    ConnectionTask discardLoop(TcpConnectionPtr connection);
private:
    string message_;
    UINT64 transferredBytes_;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _SVR_MOD_CHARGEN_H_
