///////////////////////////////////////////////////////////////////////////////

#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

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
    void initIseOptions(IseOptions& options) override;

private:
    void threadPoolTask(Thread& thread);

private:
    ThreadPool threadPool_;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _THREAD_POOL_H_
