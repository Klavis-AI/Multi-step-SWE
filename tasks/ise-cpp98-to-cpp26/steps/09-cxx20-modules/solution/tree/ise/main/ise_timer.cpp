/****************************************************************************\
*                                                                            *
*  ISE (Iris Server Engine) Project                                          *
*  http://github.com/haoxingeng/ise                                          *
*                                                                            *
*  Copyright 2013 HaoXinGeng (haoxingeng@gmail.com)                          *
*  All rights reserved.                                                      *
*                                                                            *
*  Licensed under the Apache License, Version 2.0 (the "License");           *
*  you may not use this file except in compliance with the License.          *
*  You may obtain a copy of the License at                                   *
*                                                                            *
*      http://www.apache.org/licenses/LICENSE-2.0                            *
*                                                                            *
*  Unless required by applicable law or agreed to in writing, software       *
*  distributed under the License is distributed on an "AS IS" BASIS,         *
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
*  See the License for the specific language governing permissions and       *
*  limitations under the License.                                            *
*                                                                            *
\****************************************************************************/

///////////////////////////////////////////////////////////////////////////////
// 文件名称: ise_timer.cpp
// 功能描述: 定时器的实现
///////////////////////////////////////////////////////////////////////////////

// 配置层: 宏不跨模块, 这一层永远是文本包含。
#include "ise/main/ise_options.h"
#include "ise/main/ise_global_defs.h"
#include "ise/main/ise_stldefs.h"
#include "ise/main/ise_assert.h"

// 这些平台声明以前是随项目头文件一起进来的。
#include <stdarg.h>
#include <cstdarg>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/timeb.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <stdint.h>
#include <semaphore.h>
#include <sys/file.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <type_traits>
#include <algorithm>
#include <compare>
#include <format>
#include <ranges>
#include <span>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>
#include <pthread.h>
#include <system_error>
#include <thread>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <concepts>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <deque>

import ise.timer;
import ise.errmsgs;
import ise.eventloop;
import ise.application;

using namespace ise;

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// class Timer

SeqNumberAlloc Timer::s_timerIdAlloc(1);

Timer::Timer(Timestamp expiration, INT64 interval, const TimerCallback& callback) :
    expiration_(expiration),
    interval_(interval),
    repeat_(interval > 0.0),
    timerId_(s_timerIdAlloc.allocId()),
    callback_(callback)
{
    // nothing
}

//-----------------------------------------------------------------------------

void Timer::invokeCallback()
{
    try
    {
        if (callback_)
            callback_();
    }
    catch (Exception& e)
    {
        logger().writeException(e);
    }
    catch (...)
    {}
}

//-----------------------------------------------------------------------------

void Timer::restart(Timestamp now)
{
    if (repeat_)
        expiration_ = now + interval_;
    else
        expiration_ = Timestamp(0);
}

///////////////////////////////////////////////////////////////////////////////
// class TimerQueue

TimerQueue::TimerQueue() :
    callingExpiredTimers_(false)
{
    // nothing
}

TimerQueue::~TimerQueue()
{
    clearTimers();
}

//-----------------------------------------------------------------------------

void TimerQueue::addTimer(Timer *timer)
{
    // 定时器在此处交给队列: timerIdMap_ 持有它，timerList_ 只是按到期时间排序的索引。
    // insert() 答的是一对"位置, 是否新插入"; 从前只有后一半被看一眼。
    auto [listPos, listInserted] =
        timerList_.insert(TimerItem(timer->expiration(), timer));
    ISE_ASSERT(listInserted);
    (void)listPos;
    (void)listInserted;

    auto [idPos, idInserted] =
        timerIdMap_.insert(std::make_pair(timer->timerId(), TimerHandle(timer)));
    ISE_ASSERT(idInserted);
    (void)idPos;
    (void)idInserted;

    ISE_ASSERT(timerList_.size() == timerIdMap_.size());
}

//-----------------------------------------------------------------------------

void TimerQueue::cancelTimer(TimerId timerId)
{
    if (auto iter = timerIdMap_.find(timerId); iter != timerIdMap_.end())
    {
        Timer *timer = iter->second.get();
        timerList_.erase(TimerItem(timer->expiration(), timer));
        timerIdMap_.erase(iter);      // 句柄随之释放，定时器对象销毁

        ISE_ASSERT(timerList_.size() == timerIdMap_.size());
    }
    else if (callingExpiredTimers_)
    {
        cancelingTimers_.insert(timerId);
    }
}

//-----------------------------------------------------------------------------

bool TimerQueue::getNearestExpiration(Timestamp& expiration)
{
    bool result = !timerList_.empty();
    if (result)
        expiration = timerList_.begin()->first;
    return result;
}

//-----------------------------------------------------------------------------

void TimerQueue::processExpiredTimers(Timestamp now)
{
    std::vector<TimerHandle> expiredTimers;
    TimerItem timerItem(now, reinterpret_cast<Timer*>(std::numeric_limits<uintptr_t>::max()));

    TimerList::iterator bound = timerList_.upper_bound(timerItem);
    for (TimerList::iterator iter = timerList_.begin(); iter != bound;)
    {
        Timer *timer = iter->second;
        TimerIdMap::iterator found = timerIdMap_.find(timer->timerId());
        expiredTimers.push_back(std::move(found->second));

        timerList_.erase(iter++);
        timerIdMap_.erase(found);

        ISE_ASSERT(timerList_.size() == timerIdMap_.size());
    }

    for (size_t i = 0; i < expiredTimers.size(); i++)
    {
        Timer *timer = expiredTimers[i].get();

        cancelingTimers_.clear();
        callingExpiredTimers_ = true;
        timer->invokeCallback();
        callingExpiredTimers_ = false;

        if (timer->repeat() &&
            cancelingTimers_.find(timer->timerId()) == cancelingTimers_.end())
        {
            timer->restart(now);
            // 定时器重新入队，所有权随之交回队列。
            addTimer(expiredTimers[i].release());
        }
        else
            expiredTimers[i].reset();
    }
}

//-----------------------------------------------------------------------------

void TimerQueue::clearTimers()
{
    timerList_.clear();
    timerIdMap_.clear();
}

///////////////////////////////////////////////////////////////////////////////
// class TimerManager

TimerManager::TimerManager()
{
    // nothing
}

TimerManager::~TimerManager()
{
    if (eventLoopList_)
        eventLoopList_->stop();
}

//-----------------------------------------------------------------------------
// 描述: 添加定时器 (指定时间执行)
//-----------------------------------------------------------------------------
TimerId TimerManager::executeAt(Timestamp time, const TimerCallback& callback)
{
    return getTimerEventLoop().executeAt(time, callback);
}

//-----------------------------------------------------------------------------
// 描述: 添加定时器 (在 delay 毫秒后执行)
//-----------------------------------------------------------------------------
TimerId TimerManager::executeAfter(INT64 delay, const TimerCallback& callback)
{
    return getTimerEventLoop().executeAfter(delay, callback);
}

//-----------------------------------------------------------------------------
// 描述: 添加定时器 (每 interval 毫秒循环执行)
//-----------------------------------------------------------------------------
TimerId TimerManager::executeEvery(INT64 interval, const TimerCallback& callback)
{
    return getTimerEventLoop().executeEvery(interval, callback);
}

//-----------------------------------------------------------------------------
// 描述: 取消定时器
//-----------------------------------------------------------------------------
void TimerManager::cancelTimer(TimerId timerId)
{
    getTimerEventLoop().cancelTimer(timerId);
}

//-----------------------------------------------------------------------------
// 描述: 取得用于放置定时器的事件循环
//-----------------------------------------------------------------------------
EventLoop& TimerManager::getTimerEventLoop()
{
    EventLoop *result = nullptr;

    if (iseApp().iseOptions().getServerType() & static_cast<UINT>(SERVER_TYPE::ST_TCP))
        result = iseApp().mainServer().getMainTcpServer().findEventLoop(getCurThreadId());

    if (!result)
    {
        if (!eventLoopList_)
        {
            eventLoopList_.reset(new EventLoopList(1));
            eventLoopList_->start();
        }
        result = eventLoopList_->getItem(0);
    }

    return *result;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace ise
