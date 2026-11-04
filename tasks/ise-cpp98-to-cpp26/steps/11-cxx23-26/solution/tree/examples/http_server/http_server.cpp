///////////////////////////////////////////////////////////////////////////////

#include "http_server.h"

#include <print>

IseBusiness* createIseBusinessObject()
{
    return new AppBusiness();
}

///////////////////////////////////////////////////////////////////////////////

void AppBusiness::initialize()
{
    httpServer_.setHttpSessionCallback(
        [this](const HttpRequest& request, HttpResponse& response)
        { onHttpSession(request, response); });
}

//-----------------------------------------------------------------------------

void AppBusiness::finalize()
{
    string msg = std::format("{} stoped.", getAppExeName(false));
    std::println("{}", msg);
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::afterInit()
{
    string msg = std::format("{} started.", getAppExeName(false));
    std::println("\n{}", msg);
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::onInitFailed(Exception& /*e*/)
{
    string msg = std::format("fail to start {}.", getAppExeName(false));
    std::println("\n{}", msg);
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::initIseOptions(IseOptions& options)
{
    options.setServerType(static_cast<UINT>(SERVER_TYPE::ST_TCP));
    options.setTcpServerCount(1);
    options.setTcpServerPort(8080);
    options.setTcpServerEventLoopCount(1);
}

//-----------------------------------------------------------------------------

ConnectionTask AppBusiness::onTcpConnection(TcpConnectionPtr connection)
{
    return httpServer_.onTcpConnection(std::move(connection));
}

//-----------------------------------------------------------------------------

void AppBusiness::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    httpServer_.onTcpDisconnected(connection);
}

//-----------------------------------------------------------------------------

void AppBusiness::onHttpSession(const HttpRequest& /*request*/, HttpResponse& response)
{
    string content = "this is a simple http server.";

    response.setStatusCode(200);
    response.getContentStream()->write(content.c_str(), content.length());
}
