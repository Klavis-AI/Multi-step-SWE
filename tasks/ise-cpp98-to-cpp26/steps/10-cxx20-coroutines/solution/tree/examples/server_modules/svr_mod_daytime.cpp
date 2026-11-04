///////////////////////////////////////////////////////////////////////////////

#include "svr_mod_daytime.h"

///////////////////////////////////////////////////////////////////////////////

ConnectionTask ServerModule_Daytime::onTcpConnection(TcpConnectionPtr connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    string msg = DateTime::now().toDateTimeString() + "\n";
    if (!co_await connection->send(std::as_bytes(std::span(msg))))
        co_return;
    // 这一行日志跟着发送走, 不跟着回调走: 从前它在 onTcpSendComplete()
    // 里, 现在在挂起点回来的地方。这一步换的是控制流, 不是这个程序对外
    // 记下的东西。
    logger().writeStr("onTcpSendComplete");

    connection->disconnect();
}

//-----------------------------------------------------------------------------

void ServerModule_Daytime::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}
