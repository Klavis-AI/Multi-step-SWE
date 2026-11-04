///////////////////////////////////////////////////////////////////////////////

#include "app_business.h"
#include "svr_mod_inspector.h"

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

    string msg = std::format("{} stoped.", getAppExeName(false));
    std::cout << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::afterInit()
{
    string msg = std::format("{} started.", getAppExeName(false));
    std::cout << std::endl << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::onInitFailed(Exception& /*e*/)
{
    string msg = std::format("fail to start {}.", getAppExeName(false));
    std::cout << std::endl << msg << std::endl;
    logger().writeStr(msg);
}

//-----------------------------------------------------------------------------

void AppBusiness::initIseOptions(IseOptions& options)
{
    options.setServerType(static_cast<UINT>(SERVER_TYPE::ST_TCP));
}

//-----------------------------------------------------------------------------

void AppBusiness::createServerModules(IseServerModuleList& svrModList)
{
    // 模块先由句柄持有，加入列表的那一刻把所有权交给列表的接收者。
    IseServerModuleHandle inspectorModule(new ServerModule_Inspector());
    svrModList.push_back(inspectorModule.release());
}
