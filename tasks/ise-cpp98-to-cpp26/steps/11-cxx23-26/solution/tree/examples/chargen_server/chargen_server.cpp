///////////////////////////////////////////////////////////////////////////////

#include "chargen_server.h"

#include <print>

//-----------------------------------------------------------------------------

IseBusiness* createIseBusinessObject()
{
    return new AppBusiness();
}

///////////////////////////////////////////////////////////////////////////////

void AppBusiness::initMessage()
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

void AppBusiness::initialize()
{
    initMessage();
    transferredBytes_ = 0;
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
    options.setTcpServerPort(10003);
    options.setTcpServerEventLoopCount(1);
}

//-----------------------------------------------------------------------------

ConnectionTask AppBusiness::discardLoop(TcpConnectionPtr connection)
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
// 备注: 这条连接上有两条互不相干的链 —— 一条只管收下来扔掉, 一条只管不停地发。
//       从前它们交错在 onTcpRecvComplete() 和 onTcpSendComplete() 两个回调里,
//       谁先谁后全看网络。现在各写各的: 一条连接上起两个会话协程, 两个都按值
//       收下连接对象, 于是连接活到两条链都跑完为止。
//-----------------------------------------------------------------------------

ConnectionTask AppBusiness::onTcpConnection(TcpConnectionPtr connection)
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

void AppBusiness::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    logger().writeFmt("onTcpDisconnected ({})", connection->getConnectionName());
}
