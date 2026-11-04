///////////////////////////////////////////////////////////////////////////////

#include "daytime_server.h"

//-----------------------------------------------------------------------------

IseBusiness* createIseBusinessObject()
{
    return new AppBusiness();
}

///////////////////////////////////////////////////////////////////////////////

void AppBusiness::initialize()
{
    // nothing
}

//-----------------------------------------------------------------------------

void AppBusiness::finalize()
{
    string msg = std::format("{} stoped.", getAppExeName(false));
    std::cout << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::afterInit()
{
    string msg = std::format("{} started.", getAppExeName(false));
    std::cout << std::endl << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::onInitFailed(Exception& /*e*/)
{
    string msg = std::format("fail to start {}.", getAppExeName(false));
    std::cout << std::endl << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::initIseOptions(IseOptions& options)
{
    options.setServerType(static_cast<UINT>(SERVER_TYPE::ST_TCP));
    options.setTcpServerCount(1);
    options.setTcpServerPort(10002);
    options.setTcpServerEventLoopCount(1);
}

//-----------------------------------------------------------------------------

void AppBusiness::onTcpConnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    string msg = DateTime::now().toDateTimeString() + "\n";
    connection->send(std::as_bytes(std::span(msg)));
}

//-----------------------------------------------------------------------------

void AppBusiness::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}

//-----------------------------------------------------------------------------

void AppBusiness::onTcpSendComplete(const TcpConnectionPtr& connection, const Context& /*context*/)
{
    logger().writeStr("onTcpSendComplete");

    connection->disconnect();
}
