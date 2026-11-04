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

#endif // _CHARGEN_SERVER_H_
