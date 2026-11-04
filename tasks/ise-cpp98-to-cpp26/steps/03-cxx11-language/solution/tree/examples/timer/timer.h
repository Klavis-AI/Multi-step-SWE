///////////////////////////////////////////////////////////////////////////////

#ifndef _TIMER_H_
#define _TIMER_H_

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

private:
    void onTimerAt();
    void onTimerAfter();
    void onTimerEvery();

private:
    TimerId everyTimerId_;
    int everyTimerExpiredCount_;
};

///////////////////////////////////////////////////////////////////////////////

#endif // _TIMER_H_
