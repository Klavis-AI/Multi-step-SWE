///////////////////////////////////////////////////////////////////////////////

#ifndef _CHARGEN_SERVER_H_
#define _CHARGEN_SERVER_H_

#include "ise/main/ise.h"

using namespace ise;

///////////////////////////////////////////////////////////////////////////////

class AppBusiness : public IseBusiness
{
public:
    AppBusiness() {}
    ~AppBusiness() override {}

    void initialize() override;
    void finalize() override;

    void afterInit() override;
    void onInitFailed(Exception& e) override;
    void initIseOptions(IseOptions& options) override;

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

#endif // _CHARGEN_SERVER_H_
