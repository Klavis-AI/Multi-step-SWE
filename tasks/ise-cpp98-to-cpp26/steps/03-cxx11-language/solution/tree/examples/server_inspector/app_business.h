///////////////////////////////////////////////////////////////////////////////

#ifndef _APP_BUSINESS_H_
#define _APP_BUSINESS_H_

#include "ise/main/ise.h"

using namespace ise;

///////////////////////////////////////////////////////////////////////////////

class AppBusiness : public IseSvrModBusiness
{
public:
    AppBusiness() {}
    ~AppBusiness() override {}

    void initialize() override;
    void finalize() override;

    void afterInit() override;
    void onInitFailed(Exception& e) override;
    void initIseOptions(IseOptions& options) override;

protected:
    void createServerModules(IseServerModuleList& svrModList) override;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _APP_BUSINESS_H_
