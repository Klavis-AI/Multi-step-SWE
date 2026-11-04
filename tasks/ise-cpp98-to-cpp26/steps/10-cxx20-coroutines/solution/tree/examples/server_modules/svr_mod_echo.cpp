///////////////////////////////////////////////////////////////////////////////

#include "svr_mod_echo.h"

///////////////////////////////////////////////////////////////////////////////

ConnectionTask ServerModule_Echo::onTcpConnection(TcpConnectionPtr connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    string welcome = "Welcome to the simple echo server, type 'quit' to exit.\r\n";
    if (!co_await connection->send(std::as_bytes(std::span(welcome))))
        co_return;
    // 这一行日志跟着发送走, 不跟着回调走: 从前它在 onTcpSendComplete()
    // 里, 现在在挂起点回来的地方。这一步换的是控制流, 不是这个程序对外
    // 记下的东西。
    logger().writeStr("onTcpSendComplete");

    while (true)
    {
        std::span<const std::byte> packet = co_await connection->recv(LINE_PACKET_SPLITTER);
        if (packet.empty())
            co_return;
        logger().writeStr("onTcpRecvComplete");

        string msg = trimString(toString(packet));
        logger().writeFmt("Received message: {}", msg);
        if (msg == "quit")
            break;

        // 收到的那一段原样发回去。从前这里要把指针和长度各写一遍, 两者
        // 之中任何一个写错都编译得过。
        if (!co_await connection->send(packet))
            co_return;
        logger().writeStr("onTcpSendComplete");
    }

    connection->disconnect();
}

//-----------------------------------------------------------------------------

void ServerModule_Echo::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}
