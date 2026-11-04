///////////////////////////////////////////////////////////////////////////////

#include "timer.h"

IseBusiness* createIseBusinessObject()
{
    return new AppBusiness();
}

///////////////////////////////////////////////////////////////////////////////

void AppBusiness::initialize()
{
    everyTimerId_ = 0;
    everyTimerExpiredCount_ = 0;
}

//-----------------------------------------------------------------------------

void AppBusiness::finalize()
{
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

    // after 3 seconds
    iseApp().timerManager().executeAt(Timestamp::now() + 3 * MILLISECS_PER_SECOND,
        [this]() { onTimerAt(); });

    // after 5 seconds
    iseApp().timerManager().executeAfter(5 * MILLISECS_PER_SECOND,
        [this]() { onTimerAfter(); });

    // every 1 second
    everyTimerId_ = iseApp().timerManager().executeEvery(1 * MILLISECS_PER_SECOND,
        [this]() { onTimerEvery(); });
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
    options.setServerType(0);
}

//-----------------------------------------------------------------------------

void AppBusiness::onTimerAt()
{
    logger().writeStr("onTimerAt called.");
}

//-----------------------------------------------------------------------------

void AppBusiness::onTimerAfter()
{
    logger().writeStr("onTimerAfter called.");
}

//-----------------------------------------------------------------------------

void AppBusiness::onTimerEvery()
{
    logger().writeFmt("onTimerEvery called. ({})", everyTimerExpiredCount_);

    everyTimerExpiredCount_++;
    if (everyTimerExpiredCount_ >= 10)
        iseApp().timerManager().cancelTimer(everyTimerId_);
}
