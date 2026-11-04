///////////////////////////////////////////////////////////////////////////////

#include "svr_mod_daytime.h"

///////////////////////////////////////////////////////////////////////////////

void ServerModule_Daytime::onTcpConnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    string msg = DateTime::now().toDateTimeString() + "\n";
    connection->send(std::as_bytes(std::span(msg)));
}

//-----------------------------------------------------------------------------

void ServerModule_Daytime::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}

//-----------------------------------------------------------------------------

void ServerModule_Daytime::onTcpSendComplete(const TcpConnectionPtr& connection, const Context& /*context*/)
{
    logger().writeStr("onTcpSendComplete");

    connection->disconnect();
}
