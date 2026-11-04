///////////////////////////////////////////////////////////////////////////////

#include "svr_mod_discard.h"

///////////////////////////////////////////////////////////////////////////////

void ServerModule_Discard::onTcpConnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    connection->recv(BYTE_PACKET_SPLITTER);
}

//-----------------------------------------------------------------------------

void ServerModule_Discard::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}

//-----------------------------------------------------------------------------

void ServerModule_Discard::onTcpRecvComplete(const TcpConnectionPtr& connection,
    std::span<const std::byte> packet, const Context& /*context*/)
{
    logger().writeFmt("[{}] Discarded {} bytes.",
        connection->getConnectionName(), packet.size());

    connection->recv(BYTE_PACKET_SPLITTER);
}
