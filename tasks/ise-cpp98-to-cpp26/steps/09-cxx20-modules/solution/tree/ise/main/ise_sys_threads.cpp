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
// 文件名称: ise_sys_threads.cpp
// 功能描述: ISE系统线程
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
#include <cstdlib>
#include <stdint.h>
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <stdlib.h>
#include <time.h>
#include <sys/timeb.h>
#include <iostream>
#include <fstream>
#include <string>
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
#include <pthread.h>
#include <system_error>
#include <thread>
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
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <deque>
#include <concepts>

import ise.application;

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// class SysThread

SysThread::SysThread(SysThreadMgr& threadMgr) :
    threadMgr_(threadMgr)
{
    setAutoDelete(true);
    threadMgr_.registerThread(this);
}

SysThread::~SysThread()
{
    threadMgr_.unregisterThread(this);
}

///////////////////////////////////////////////////////////////////////////////
// class SysDaemonThread

void SysDaemonThread::execute()
{
    int secondCount = 0;

    while (!isTerminated())
    {
        try
        {
            iseApp().iseBusiness().daemonThreadExecute(*this, secondCount);
        }
        catch (Exception& e)
        {
            logger().writeException(e);
        }

        secondCount++;
        this->sleep(1);
    }
}

///////////////////////////////////////////////////////////////////////////////
// class SysThreadMgr

void SysThreadMgr::initialize()
{
    threadList_.clear();

    // 线程自行销毁(见 threadFinalProc)，run() 成功后所有权交给它自己；
    // threadList_ 只登记它。
    std::unique_ptr<Thread> thread(new SysDaemonThread(*this));
    threadList_.add(thread.get());
    thread->run();
    thread.release();
}

//-----------------------------------------------------------------------------

void SysThreadMgr::finalize()
{
    const int MAX_WAIT_FOR_SECS = 5;
    int killedCount = 0;

    threadList_.waitForAllThreads(MAX_WAIT_FOR_SECS, &killedCount);

    if (killedCount > 0)
        logger().writeStr(formatString(SEM_THREAD_KILLED, killedCount, "system"));
}

//-----------------------------------------------------------------------------

void SysThreadMgr::registerThread(SysThread *thread)
{
    threadList_.add(thread);
}

//-----------------------------------------------------------------------------

void SysThreadMgr::unregisterThread(SysThread *thread)
{
    threadList_.remove(thread);
}

///////////////////////////////////////////////////////////////////////////////

} // namespace ise
