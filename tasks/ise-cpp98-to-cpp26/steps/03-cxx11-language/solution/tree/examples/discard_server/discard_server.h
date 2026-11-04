///////////////////////////////////////////////////////////////////////////////

#ifndef _DISCARD_SERVER_H_
#define _DISCARD_SERVER_H_

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
    void onTcpRecvComplete(const TcpConnectionPtr& connection, void *packetBuffer,
        int packetSize, const Context& context) override;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _DISCARD_SERVER_H_
