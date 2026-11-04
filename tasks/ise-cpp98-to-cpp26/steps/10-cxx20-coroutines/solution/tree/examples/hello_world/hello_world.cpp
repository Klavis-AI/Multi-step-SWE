///////////////////////////////////////////////////////////////////////////////

#include "hello_world.h"

//-----------------------------------------------------------------------------

IseBusiness* createIseBusinessObject()
{
    return new AppBusiness();
}

///////////////////////////////////////////////////////////////////////////////

void AppBusiness::initIseOptions(IseOptions& options)
{
    options.setServerType(static_cast<UINT>(SERVER_TYPE::ST_TCP));
    options.setTcpServerPort(12345);
}

//-----------------------------------------------------------------------------

ConnectionTask AppBusiness::onTcpConnection(TcpConnectionPtr connection)
{
    string msg = "Hello World!\r\n";
    co_await connection->send(std::as_bytes(std::span(msg)));
    connection->disconnect();
}
