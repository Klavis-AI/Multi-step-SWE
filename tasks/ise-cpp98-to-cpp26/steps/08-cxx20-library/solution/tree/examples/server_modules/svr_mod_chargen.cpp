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

void ServerModule_Chargen::onTcpConnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    connection->setNoDelay(true);

    connection->recv();
    connection->send(std::as_bytes(std::span(message_)));
}

//-----------------------------------------------------------------------------

void ServerModule_Chargen::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}

//-----------------------------------------------------------------------------

void ServerModule_Chargen::onTcpRecvComplete(const TcpConnectionPtr& connection,
    std::span<const std::byte> packet, const Context& /*context*/)
{
    logger().writeFmt("[{}] Discarded {} bytes.",
        connection->getConnectionName(), packet.size());

    connection->recv();
}

//-----------------------------------------------------------------------------

void ServerModule_Chargen::onTcpSendComplete(const TcpConnectionPtr& connection, const Context& /*context*/)
{
    transferredBytes_ += message_.length();
    connection->send(std::as_bytes(std::span(message_)));
}
