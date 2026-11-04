///////////////////////////////////////////////////////////////////////////////

#include "svr_mod_discard.h"

///////////////////////////////////////////////////////////////////////////////

ConnectionTask ServerModule_Discard::onTcpConnection(TcpConnectionPtr connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    while (true)
    {
        std::span<const std::byte> packet = co_await connection->recv(BYTE_PACKET_SPLITTER);
        if (packet.empty())
            co_return;

        logger().writeFmt("[{}] Discarded {} bytes.",
            connection->getConnectionName(), packet.size());
    }
}

//-----------------------------------------------------------------------------

void ServerModule_Discard::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}
