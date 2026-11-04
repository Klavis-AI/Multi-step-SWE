///////////////////////////////////////////////////////////////////////////////

#ifndef _UDP_CLIENT_H_
#define _UDP_CLIENT_H_

#include "ise/main/ise.h"

using namespace ise;

///////////////////////////////////////////////////////////////////////////////

class AppBusiness : public IseBusiness
{
public:
    AppBusiness() {}
    ~AppBusiness() override {}

    void initialize() override;
    void initIseOptions(IseOptions& options) override;
    void assistorThreadExecute(AssistorThread& assistorThread, int assistorIndex) override;

private:
    InetAddress serverAddr_;
    std::unique_ptr<BaseUdpClient> udpClient_;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _UDP_CLIENT_H_
