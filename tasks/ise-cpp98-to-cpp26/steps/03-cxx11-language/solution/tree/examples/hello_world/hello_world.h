///////////////////////////////////////////////////////////////////////////////

#ifndef _HELLO_WORLD_H_
#define _HELLO_WORLD_H_

#include "ise/main/ise.h"

using namespace ise;

///////////////////////////////////////////////////////////////////////////////

class AppBusiness : public IseBusiness
{
public:
    void initIseOptions(IseOptions& options) override;
    void onTcpConnected(const TcpConnectionPtr& connection) override;
    void onTcpSendComplete(const TcpConnectionPtr& connection, const Context& context) override;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _HELLO_WORLD_H_
