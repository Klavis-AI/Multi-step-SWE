///////////////////////////////////////////////////////////////////////////////

#ifndef _CHARGEN_CLIENT_H_
#define _CHARGEN_CLIENT_H_

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

    bool parseArguments(int argc, char *argv[]) override;
    string getAppHelp() override;
    void afterInit() override;
    void initIseOptions(IseOptions& options) override;

    ConnectionTask onTcpConnection(TcpConnectionPtr connection) override;
    void onTcpDisconnected(const TcpConnectionPtr& connection) override;

private:
    void onConnectComplete(bool success, TcpConnection *connection,
        const InetAddress& peerAddr, const Context& context);
};

///////////////////////////////////////////////////////////////////////////////

#endif // _CHARGEN_CLIENT_H_
