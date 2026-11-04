///////////////////////////////////////////////////////////////////////////////

#include "app_business.h"
#include "svr_mod_1.h"
#include "svr_mod_2.h"

//-----------------------------------------------------------------------------

IseBusiness* createIseBusinessObject()
{
    return new AppBusiness();
}

///////////////////////////////////////////////////////////////////////////////

void AppBusiness::initialize()
{
    IseSvrModBusiness::initialize();
}

//-----------------------------------------------------------------------------

void AppBusiness::finalize()
{
    IseSvrModBusiness::finalize();

    string msg = formatString("%s stoped.", getAppExeName(false).c_str());
    std::cout << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::afterInit()
{
    string msg = formatString("%s started.", getAppExeName(false).c_str());
    std::cout << std::endl << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::onInitFailed(Exception& /*e*/)
{
    string msg = formatString("fail to start %s.", getAppExeName(false).c_str());
    std::cout << std::endl << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::initIseOptions(IseOptions& options)
{
    options.setIsDaemon(false);
}

//-----------------------------------------------------------------------------

void AppBusiness::createServerModules(IseServerModuleList& svrModList)
{
    // 每个模块先由句柄持有，加入列表的那一刻把所有权交给列表的接收者。
    IseServerModuleHandle module1(new ServerModule_1());
    svrModList.push_back(module1.release());

    IseServerModuleHandle module2(new ServerModule_2());
    svrModList.push_back(module2.release());
}
