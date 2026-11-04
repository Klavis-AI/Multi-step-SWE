///////////////////////////////////////////////////////////////////////////////

#include "thread_pool.h"

//-----------------------------------------------------------------------------

IseBusiness* createIseBusinessObject()
{
    return new AppBusiness();
}

///////////////////////////////////////////////////////////////////////////////

void AppBusiness::initialize()
{
    threadPool_.addTask([this](Thread& thread) { threadPoolTask(thread); });
    threadPool_.setTaskRepeat(true);
    threadPool_.start(10);
}

//-----------------------------------------------------------------------------

void AppBusiness::finalize()
{
    threadPool_.stop();
}

//-----------------------------------------------------------------------------

void AppBusiness::initIseOptions(IseOptions& options)
{
    options.setIsDaemon(false);
}

//-----------------------------------------------------------------------------

void AppBusiness::threadPoolTask(Thread& thread)
{
    static AtomicInt counter;

    counter.increment();
    string s = std::format("executing task: {}. (thread: {})", counter.get(), thread.getThreadId());
    logger().writeStr(s);
    std::cout << s << std::endl;

    thread.sleep(1);
}
