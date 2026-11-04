///////////////////////////////////////////////////////////////////////////////

#include "echo_server.h"

IseBusiness* createIseBusinessObject()
{
    return new AppBusiness();
}

///////////////////////////////////////////////////////////////////////////////

const int RECV_TIMEOUT = 1000*5;  // ms

//-----------------------------------------------------------------------------
// 描述: 初始化 (失败则抛出异常)
//-----------------------------------------------------------------------------
void AppBusiness::initialize()
{
    // nothing
}

//-----------------------------------------------------------------------------
// 描述: 结束化 (无论初始化是否有异常，结束时都会执行)
//-----------------------------------------------------------------------------
void AppBusiness::finalize()
{
    string msg = std::format("{} stoped.", getAppExeName(false));
    std::cout << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------
// 描述: 初始化成功之后
//-----------------------------------------------------------------------------
void AppBusiness::afterInit()
{
    string msg = std::format("{} started.", getAppExeName(false));
    std::cout << std::endl << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------
// 描述: 初始化失败
//-----------------------------------------------------------------------------
void AppBusiness::onInitFailed(Exception& /*e*/)
{
    string msg = std::format("fail to start {}.", getAppExeName(false));
    std::cout << std::endl << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------
// 描述: 初始化ISE配置信息
//-----------------------------------------------------------------------------
void AppBusiness::initIseOptions(IseOptions& options)
{
    // 设置服务器类型
    options.setServerType(static_cast<UINT>(SERVER_TYPE::ST_TCP));
    // 设置TCP服务器的总数
    options.setTcpServerCount(1);
    // 设置TCP服务端口号
    options.setTcpServerPort(10000);
    // 设置TCP事件循环的个数
    options.setTcpServerEventLoopCount(1);
}

//-----------------------------------------------------------------------------
// 描述: 接受了一个新的TCP连接
//-----------------------------------------------------------------------------
void AppBusiness::onTcpConnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpConnected ({}) (ConnCount: {})",
        connection->getPeerAddr().getDisplayStr(),
        connection->getServerConnCount());

    string msg = "Welcome to the simple echo server, type 'quit' to exit.\r\n";
    connection->send(std::as_bytes(std::span(msg)));
}

//-----------------------------------------------------------------------------
// 描述: 断开了一个TCP连接
//-----------------------------------------------------------------------------
void AppBusiness::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}

//-----------------------------------------------------------------------------
// 描述: TCP连接上的一个接收任务已完成
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
// 描述: TCP连接上的一个发送任务已完成
//-----------------------------------------------------------------------------
void AppBusiness::onTcpSendComplete(const TcpConnectionPtr& connection, const Context& /*context*/)
{
    logger().writeStr("onTcpSendComplete");

    connection->recv(LINE_PACKET_SPLITTER, EMPTY_CONTEXT, RECV_TIMEOUT);
}
