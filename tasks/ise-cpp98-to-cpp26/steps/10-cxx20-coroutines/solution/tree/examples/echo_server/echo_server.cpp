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
// 描述: 一条TCP连接的整条会话
// 备注: 从前这是四个回调函数, 一问一答的次序靠"发完了就去收、收到了就去发"这
//       两条回调之间的跳转维持。现在它就是这个函数自上而下的次序。
//-----------------------------------------------------------------------------
ConnectionTask AppBusiness::onTcpConnection(TcpConnectionPtr connection)
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
        // 收一行。连接断开或超时时醒来的那一次拿到的是空的一段。
        std::span<const std::byte> packet =
            co_await connection->recv(LINE_PACKET_SPLITTER, RECV_TIMEOUT);
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
// 描述: 断开了一个TCP连接
//-----------------------------------------------------------------------------
void AppBusiness::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}
