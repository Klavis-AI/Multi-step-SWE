///////////////////////////////////////////////////////////////////////////////

#include "echo_server.h"

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
    options.setTcpServerPort(10000);
    options.setTcpServerEventLoopCount(1);
}

//-----------------------------------------------------------------------------

void AppBusiness::onTcpConnected(const TcpConnectionPtr& connection)
{
    const int MAX_CONN_COUNT = 3;

    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    int connCount = connection->getServerConnCount();
    if (connCount > MAX_CONN_COUNT)
    {
        logger().writeFmt("Too many connections. (ConnCount: {})", connCount);
        connection->disconnect();
    }
    else
    {
        string msg = "Welcome to the simple echo server, type 'quit' to exit.\r\n";
        connection->send(std::as_bytes(std::span(msg)));
    }
}

//-----------------------------------------------------------------------------

void AppBusiness::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}

//-----------------------------------------------------------------------------

void AppBusiness::onTcpRecvComplete(const TcpConnectionPtr& connection,
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

void AppBusiness::onTcpSendComplete(const TcpConnectionPtr& connection, const Context& /*context*/)
{
    logger().writeStr("onTcpSendComplete");

    connection->recv(LINE_PACKET_SPLITTER);
}
