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
// 文件名称: ise_server_assistor.cpp
// 功能描述: 辅助服务器的实现
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
#include <optional>
#include <string_view>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
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

import ise.server;
import ise.errmsgs;
import ise.application;

using namespace ise;

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// class AssistorThread

AssistorThread::AssistorThread(AssistorThreadPool *threadPool, int assistorIndex) :
    ownPool_(threadPool),
    assistorIndex_(assistorIndex)
{
    setAutoDelete(true);
    ownPool_->registerThread(this);
}

AssistorThread::~AssistorThread()
{
    ownPool_->unregisterThread(this);
}

//-----------------------------------------------------------------------------
// 描述: 线程执行函数
//-----------------------------------------------------------------------------
void AssistorThread::execute()
{
    ownPool_->getAssistorServer().onAssistorThreadExecute(*this, assistorIndex_);
}

//-----------------------------------------------------------------------------
// 描述: 执行 kill() 前的附加操作
//-----------------------------------------------------------------------------
void AssistorThread::doKill()
{
    // nothing
}

///////////////////////////////////////////////////////////////////////////////
// class AssistorThreadPool

AssistorThreadPool::AssistorThreadPool(AssistorServer *ownAssistorServer) :
    ownAssistorSvr_(ownAssistorServer)
{
    // nothing
}

AssistorThreadPool::~AssistorThreadPool()
{
    // nothing
}

//-----------------------------------------------------------------------------
// 描述: 注册线程
//-----------------------------------------------------------------------------
void AssistorThreadPool::registerThread(AssistorThread *thread)
{
    threadList_.add(thread);
}

//-----------------------------------------------------------------------------
// 描述: 注销线程
//-----------------------------------------------------------------------------
void AssistorThreadPool::unregisterThread(AssistorThread *thread)
{
    threadList_.remove(thread);
}

//-----------------------------------------------------------------------------
// 描述: 通知所有线程退出
//-----------------------------------------------------------------------------
void AssistorThreadPool::terminateAllThreads()
{
    threadList_.terminateAllThreads();
}

//-----------------------------------------------------------------------------
// 描述: 等待所有线程退出
//-----------------------------------------------------------------------------
void AssistorThreadPool::waitForAllThreads()
{
    if (threadList_.getCount() > 0)
        logger().writeStr(formatString(SEM_WAIT_FOR_THREADS, "assistor"));

    threadList_.waitForAllThreads(TIMEOUT_INFINITE);
}

//-----------------------------------------------------------------------------
// 描述: 打断指定线程的睡眠
//-----------------------------------------------------------------------------
void AssistorThreadPool::interruptThreadSleep(int assistorIndex)
{
    AutoLocker locker(threadList_.getMutex());

    auto indices = std::views::iota(0, threadList_.getCount());
    auto found = std::ranges::find_if(indices, [&](int i)
        { return ((AssistorThread*)threadList_[i])->getIndex() == assistorIndex; });

    if (found != indices.end())
        ((AssistorThread*)threadList_[*found])->interruptSleep();
}

///////////////////////////////////////////////////////////////////////////////
// class AssistorServer

AssistorServer::AssistorServer() :
    isActive_(false),
    threadPool_(this)
{
    // nothing
}

AssistorServer::~AssistorServer()
{
    // nothing
}

//-----------------------------------------------------------------------------
// 描述: 启动服务器
//-----------------------------------------------------------------------------
void AssistorServer::open()
{
    if (!isActive_)
    {
        int count = iseApp().iseOptions().getAssistorThreadCount();

        for (int i = 0; i < count; i++)
        {
            // 线程自行销毁(见 threadFinalProc)，run() 成功后所有权交给它自己。
            std::unique_ptr<AssistorThread> thread(new AssistorThread(&threadPool_, i));
            thread->run();
            thread.release();
        }

        isActive_ = true;
    }
}

//-----------------------------------------------------------------------------
// 描述: 关闭服务器
//-----------------------------------------------------------------------------
void AssistorServer::close()
{
    if (isActive_)
    {
        waitForAllAssistorThreads();
        isActive_ = false;
    }
}

//-----------------------------------------------------------------------------
// 描述: 辅助服务线程执行函数
// 参数:
//   assistorIndex - 辅助线程序号(0-based)
//-----------------------------------------------------------------------------
void AssistorServer::onAssistorThreadExecute(AssistorThread& assistorThread, int assistorIndex)
{
    iseApp().iseBusiness().assistorThreadExecute(assistorThread, assistorIndex);
}

//-----------------------------------------------------------------------------
// 描述: 通知所有辅助线程退出
//-----------------------------------------------------------------------------
void AssistorServer::terminateAllAssistorThreads()
{
    threadPool_.terminateAllThreads();
}

//-----------------------------------------------------------------------------
// 描述: 等待所有辅助线程退出
//-----------------------------------------------------------------------------
void AssistorServer::waitForAllAssistorThreads()
{
    threadPool_.waitForAllThreads();
}

//-----------------------------------------------------------------------------
// 描述: 打断指定辅助线程的睡眠
//-----------------------------------------------------------------------------
void AssistorServer::interruptAssistorThreadSleep(int assistorIndex)
{
    threadPool_.interruptThreadSleep(assistorIndex);
}

///////////////////////////////////////////////////////////////////////////////

} // namespace ise
