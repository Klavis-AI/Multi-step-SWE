///////////////////////////////////////////////////////////////////////////////

#ifndef _HTTP_SERVER_H_
#define _HTTP_SERVER_H_

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

    void onHttpSession(const HttpRequest& request, HttpResponse& response);

private:
    HttpServer httpServer_;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _HTTP_SERVER_H_
