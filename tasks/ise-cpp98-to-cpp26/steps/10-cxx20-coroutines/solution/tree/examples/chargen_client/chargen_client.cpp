///////////////////////////////////////////////////////////////////////////////

#include "chargen_client.h"

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
    // nothing
}

//-----------------------------------------------------------------------------

bool AppBusiness::parseArguments(int argc, char */*argv*/[])
{
    if (argc <= 1)
    {
        std::cout << getAppHelp() << std::endl;
        return false;
    }
    else
        return true;
}

//-----------------------------------------------------------------------------

string AppBusiness::getAppHelp()
{
    return std::format("Usage: {} ip\n",
        extractFileName(getAppExeName()));
}

//-----------------------------------------------------------------------------

void AppBusiness::afterInit()
{
    string ip = iseApp().getArgString(0);
    int port = 10003;

    iseApp().tcpConnector().connect(InetAddress(ip, port),
        [this](bool success, TcpConnection *connection,
            const InetAddress& peerAddr, const Context& context)
        { onConnectComplete(success, connection, peerAddr, context); });
}

//-----------------------------------------------------------------------------

void AppBusiness::initIseOptions(IseOptions& options)
{
    options.setIsDaemon(false);
    options.setServerType(static_cast<UINT>(SERVER_TYPE::ST_TCP));
    options.setTcpServerEventLoopCount(1);
}

//-----------------------------------------------------------------------------

ConnectionTask AppBusiness::onTcpConnection(TcpConnectionPtr connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    while (true)
    {
        std::span<const std::byte> packet = co_await connection->recv();
        if (packet.empty())
            co_return;

        logger().writeFmt("[{}] Discarded {} bytes.",
            connection->getConnectionName(), packet.size());

        std::cout << toString(packet);
    }
}

//-----------------------------------------------------------------------------

void AppBusiness::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}


//-----------------------------------------------------------------------------

void AppBusiness::onConnectComplete(bool success,  TcpConnection */*connection*/,
    const InetAddress& peerAddr, const Context& /*context*/)
{
    logger().writeFmt("connect to {} {}.", peerAddr.getDisplayStr(), success? "successfully" : "failed");
}
