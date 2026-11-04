///////////////////////////////////////////////////////////////////////////////

#include "svr_mod_echo.h"

///////////////////////////////////////////////////////////////////////////////

void ServerModule_Echo::onTcpConnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    string msg = "Welcome to the simple echo server, type 'quit' to exit.\r\n";
    connection->send(std::as_bytes(std::span(msg)));
}

//-----------------------------------------------------------------------------

void ServerModule_Echo::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}

//-----------------------------------------------------------------------------

void ServerModule_Echo::onTcpRecvComplete(const TcpConnectionPtr& connection,
    std::span<const std::byte> packet, const Context& /*context*/)
{
    logger().writeStr("onTcpRecvComplete");

    string msg(reinterpret_cast<const char*>(packet.data()), packet.size());
    msg = trimString(msg);
    if (msg == "quit")
        connection->disconnect();
    else
        // 收到的那一段原样发回去。从前这里要把指针和长度各写一遍, 两者
        // 之中任何一个写错都编译得过。
        connection->send(packet);

    logger().writeFmt("Received message: {}", msg);
}

//-----------------------------------------------------------------------------

void ServerModule_Echo::onTcpSendComplete(const TcpConnectionPtr& connection, const Context& /*context*/)
{
    logger().writeStr("onTcpSendComplete");

    connection->recv(LINE_PACKET_SPLITTER, EMPTY_CONTEXT);
}
