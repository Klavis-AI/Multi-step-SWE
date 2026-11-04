///////////////////////////////////////////////////////////////////////////////

#ifndef _UDP_SERVER_H_
#define _UDP_SERVER_H_

#include "ise/main/ise.h"

using namespace ise;

///////////////////////////////////////////////////////////////////////////////

class AppBusiness : public IseBusiness
{
public:
    AppBusiness() {}
    ~AppBusiness() override {}

    void initialize() override;
    void finalize() override;

    void afterInit() override;
    void onInitFailed(Exception& e) override;
    void initIseOptions(IseOptions& options) override;

    void onRecvedUdpPacket(UdpWorkerThread& workerThread, int groupIndex, UdpPacket& packet) override;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _UDP_SERVER_H_
