///////////////////////////////////////////////////////////////////////////////

#include "daytime_server.h"

#include <print>

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
    string msg = std::format("{} stoped.", getAppExeName(false));
    std::println("{}", msg);
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::afterInit()
{
    string msg = std::format("{} started.", getAppExeName(false));
    std::println("\n{}", msg);
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::onInitFailed(Exception& /*e*/)
{
    string msg = std::format("fail to start {}.", getAppExeName(false));
    std::println("\n{}", msg);
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::initIseOptions(IseOptions& options)
{
    options.setServerType(static_cast<UINT>(SERVER_TYPE::ST_TCP));
    options.setTcpServerCount(1);
    options.setTcpServerPort(10002);
    options.setTcpServerEventLoopCount(1);
}

//-----------------------------------------------------------------------------

ConnectionTask AppBusiness::onTcpConnection(TcpConnectionPtr connection)
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

    // 报完时就挂断。从前这一行在 onTcpSendComplete() 里, 离上一行隔着一次事件
    // 循环的往返和一个必须自己记住"刚才发的是什么"的 Context。
    connection->disconnect();
}

//-----------------------------------------------------------------------------

void AppBusiness::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}
