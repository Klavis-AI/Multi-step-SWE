///////////////////////////////////////////////////////////////////////////////

#include "svr_mod_chargen.h"

///////////////////////////////////////////////////////////////////////////////

ServerModule_Chargen::ServerModule_Chargen()
{
    initMessage();
    transferredBytes_ = 0;
}

//-----------------------------------------------------------------------------

void ServerModule_Chargen::initMessage()
{
    string line;
    for (int i = 33; i < 127; ++i)
        line.push_back(char(i));
    line += line;

    message_.clear();
    for (size_t i = 0; i < 127-33; ++i)
        message_ += line.substr(i, 72) + '\n';
}

//-----------------------------------------------------------------------------

ConnectionTask ServerModule_Chargen::discardLoop(TcpConnectionPtr connection)
{
    while (true)
    {
        std::span<const std::byte> packet = co_await connection->recv();
        if (packet.empty())
            co_return;

        logger().writeFmt("[{}] Discarded {} bytes.",
            connection->getConnectionName(), packet.size());
    }
}

//-----------------------------------------------------------------------------
// 备注: 收和发是两条互不相干的链, 因此一条连接上起两个会话协程。
//-----------------------------------------------------------------------------

ConnectionTask ServerModule_Chargen::onTcpConnection(TcpConnectionPtr connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    connection->setNoDelay(true);

    discardLoop(connection);

    while (co_await connection->send(std::as_bytes(std::span(message_))))
        transferredBytes_ += message_.length();
}

//-----------------------------------------------------------------------------

void ServerModule_Chargen::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}
