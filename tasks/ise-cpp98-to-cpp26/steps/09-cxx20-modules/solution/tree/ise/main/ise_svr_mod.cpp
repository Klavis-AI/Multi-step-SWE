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
// 文件名称: ise_svr_mod.cpp
// 功能描述: 服务模块支持
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

import ise.svrmod;

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// classs IseServerModule

void IseServerModule::broadcastMessage(BaseSvrModMessage& message)
{
    (static_cast<IseSvrModBusiness*>(&iseApp().iseBusiness()))->broadcastMessage(message);
}

///////////////////////////////////////////////////////////////////////////////
// classs IseServerModuleMgr

IseServerModuleMgr::IseServerModuleMgr()
{
    // nothing
}

IseServerModuleMgr::~IseServerModuleMgr()
{
    clearServerModuleList();
}

//-----------------------------------------------------------------------------
// 描述: 初始化服务模块列表
//-----------------------------------------------------------------------------
void IseServerModuleMgr::initServerModuleList(const IseServerModuleList& list)
{
    // 注: 本函数接管 list 中各模块的所有权，逐个装进句柄，此后由 items_ 持有。
    items_.clear();
    for (size_t i = 0; i < list.size(); ++i)
    {
        IseServerModuleHandle module(list[i]);
        module->svrModIndex_ = static_cast<int>(i);
        items_.push_back(std::move(module));
    }
}

//-----------------------------------------------------------------------------
// 描述: 清空所有服务模块
//-----------------------------------------------------------------------------
void IseServerModuleMgr::clearServerModuleList()
{
    items_.clear();
}

///////////////////////////////////////////////////////////////////////////////
// class IseSvrModBusiness

//-----------------------------------------------------------------------------
// 描述: 初始化 (失败则抛出异常)
//-----------------------------------------------------------------------------
void IseSvrModBusiness::initialize()
{
    IseBusiness::initialize();

    // 创建所有服务模块
    IseServerModuleList svrModList;
    createServerModules(svrModList);
    serverModuleMgr_.initServerModuleList(svrModList);

    // 初始化各个映射表
    initActionCodeMap();
    initUdpGroupIndexMap();
    initTcpServerIndexMap();

    // 更新ISE配置
    updateIseOptions();
}

//-----------------------------------------------------------------------------
// 描述: 结束化 (无论初始化是否有异常，结束时都会执行)
//-----------------------------------------------------------------------------
void IseSvrModBusiness::finalize()
{
    try { serverModuleMgr_.clearServerModuleList(); } catch (...) {}

    IseBusiness::finalize();
}

//-----------------------------------------------------------------------------
// 描述: UDP数据包分类
//-----------------------------------------------------------------------------
void IseSvrModBusiness::classifyUdpPacket(std::span<const std::byte> packet, int& groupIndex)
{
    groupIndex = -1;
    if (packet.empty() || !filterUdpPacket(packet)) return;

    UINT actionCode = getUdpPacketActionCode(packet);
    if (auto iter = actionCodeMap_.find(actionCode); iter != actionCodeMap_.end())
    {
        groupIndex = iter->second;
    }
}

//-----------------------------------------------------------------------------
// 描述: 收到了UDP数据包
//-----------------------------------------------------------------------------
void IseSvrModBusiness::onRecvedUdpPacket(UdpWorkerThread& workerThread,
    int groupIndex, UdpPacket& packet)
{
    if (auto iter = udpGroupIndexMap_.find(groupIndex); iter != udpGroupIndexMap_.end())
    {
        int modIndex = iter->second;
        serverModuleMgr_.getItem(modIndex).onRecvedUdpPacket(workerThread, packet);
    }
}

//-----------------------------------------------------------------------------
// 描述: 接受了一个新的TCP连接
//-----------------------------------------------------------------------------
void IseSvrModBusiness::onTcpConnected(const TcpConnectionPtr& connection)
{
    if (auto iter = tcpServerIndexMap_.find(connection->getServerIndex()); iter != tcpServerIndexMap_.end())
    {
        int modIndex = iter->second;
        serverModuleMgr_.getItem(modIndex).onTcpConnected(connection);
    }
}

//-----------------------------------------------------------------------------
// 描述: 断开了一个TCP连接
//-----------------------------------------------------------------------------
void IseSvrModBusiness::onTcpDisconnected(const TcpConnectionPtr& connection)
{
    if (auto iter = tcpServerIndexMap_.find(connection->getServerIndex()); iter != tcpServerIndexMap_.end())
    {
        int modIndex = iter->second;
        serverModuleMgr_.getItem(modIndex).onTcpDisconnected(connection);
    }
}

//-----------------------------------------------------------------------------
// 描述: TCP连接上的一个接收任务已完成
//-----------------------------------------------------------------------------
void IseSvrModBusiness::onTcpRecvComplete(const TcpConnectionPtr& connection,
    std::span<const std::byte> packet, const Context& context)
{
    if (auto iter = tcpServerIndexMap_.find(connection->getServerIndex()); iter != tcpServerIndexMap_.end())
    {
        int modIndex = iter->second;
        serverModuleMgr_.getItem(modIndex).onTcpRecvComplete(connection, packet, context);
    }
}

//-----------------------------------------------------------------------------
// 描述: TCP连接上的一个发送任务已完成
//-----------------------------------------------------------------------------
void IseSvrModBusiness::onTcpSendComplete(const TcpConnectionPtr& connection, const Context& context)
{
    if (auto iter = tcpServerIndexMap_.find(connection->getServerIndex()); iter != tcpServerIndexMap_.end())
    {
        int modIndex = iter->second;
        serverModuleMgr_.getItem(modIndex).onTcpSendComplete(connection, context);
    }
}

//-----------------------------------------------------------------------------
// 描述: 辅助服务线程执行(assistorIndex: 0-based)
//-----------------------------------------------------------------------------
void IseSvrModBusiness::assistorThreadExecute(AssistorThread& assistorThread, int assistorIndex)
{
    int index1, index2 = 0;

    for (int i = 0; i < serverModuleMgr_.getCount(); i++)
    {
        index1 = index2;
        index2 += serverModuleMgr_.getItem(i).getAssistorThreadCount();

        if (assistorIndex >= index1 && assistorIndex < index2)
        {
            serverModuleMgr_.getItem(i).assistorThreadExecute(assistorThread, assistorIndex - index1);
            break;
        }
    }
}

//-----------------------------------------------------------------------------
// 描述: 系统守护线程执行 (secondCount: 0-based)
//-----------------------------------------------------------------------------
void IseSvrModBusiness::daemonThreadExecute(Thread& thread, int secondCount)
{
    for (int i = 0; i < serverModuleMgr_.getCount(); i++)
        serverModuleMgr_.getItem(i).daemonThreadExecute(thread, secondCount);
}

//-----------------------------------------------------------------------------
// 描述: 根据服务模块序号和模块内的局部辅助线程序号，取得此辅助线程的全局序号
//-----------------------------------------------------------------------------
int IseSvrModBusiness::getAssistorIndex(int serverModuleIndex, int localAssistorIndex)
{
    int result = -1;

    if (serverModuleIndex >= 0 && serverModuleIndex < serverModuleMgr_.getCount())
    {
        result = 0;
        for (int i = 0; i < serverModuleIndex; i++)
            result += serverModuleMgr_.getItem(i).getAssistorThreadCount();
        result += localAssistorIndex;
    }

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 广播消息给服务模块
//-----------------------------------------------------------------------------
void IseSvrModBusiness::broadcastMessage(BaseSvrModMessage& message)
{
    for (int i = 0; i < serverModuleMgr_.getCount(); i++)
    {
        if (message.isHandled) break;
        serverModuleMgr_.getItem(i).onSvrModMessage(message);
    }
}

//-----------------------------------------------------------------------------
// 描述: 取得全局UDP组别数量
//-----------------------------------------------------------------------------
int IseSvrModBusiness::getUdpGroupCount()
{
    int result = 0;

    for (int i = 0; i < serverModuleMgr_.getCount(); i++)
    {
        IseServerModule& serverModule = serverModuleMgr_.getItem(i);
        result += serverModule.getUdpGroupCount();
    }

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 取得全局TCP服务器数量
//-----------------------------------------------------------------------------
int IseSvrModBusiness::getTcpServerCount()
{
    int result = 0;

    for (int i = 0; i < serverModuleMgr_.getCount(); i++)
    {
        IseServerModule& serverModule = serverModuleMgr_.getItem(i);
        result += serverModule.getTcpServerCount();
    }

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 初始化 actionCodeMap_
//-----------------------------------------------------------------------------
void IseSvrModBusiness::initActionCodeMap()
{
    int globalGroupIndex = 0;
    actionCodeMap_.clear();

    for (int modIndex = 0; modIndex < serverModuleMgr_.getCount(); modIndex++)
    {
        IseServerModule& serverModule = serverModuleMgr_.getItem(modIndex);
        for (int groupIndex = 0; groupIndex < serverModule.getUdpGroupCount(); groupIndex++)
        {
            ActionCodeArray actionCodes;
            serverModule.getUdpGroupActionCodes(groupIndex, actionCodes);

            for (UINT i = 0; i < actionCodes.size(); i++)
                actionCodeMap_[actionCodes[i]] = globalGroupIndex;

            globalGroupIndex++;
        }
    }
}

//-----------------------------------------------------------------------------
// 描述: 初始化 udpGroupIndexMap_
//-----------------------------------------------------------------------------
void IseSvrModBusiness::initUdpGroupIndexMap()
{
    int globalGroupIndex = 0;
    udpGroupIndexMap_.clear();

    for (int modIndex = 0; modIndex < serverModuleMgr_.getCount(); modIndex++)
    {
        IseServerModule& serverModule = serverModuleMgr_.getItem(modIndex);
        for (int groupIndex = 0; groupIndex < serverModule.getUdpGroupCount(); groupIndex++)
        {
            udpGroupIndexMap_[globalGroupIndex] = modIndex;
            globalGroupIndex++;
        }
    }
}

//-----------------------------------------------------------------------------
// 描述: 初始化 tcpServerIndexMap_
//-----------------------------------------------------------------------------
void IseSvrModBusiness::initTcpServerIndexMap()
{
    int globalServerIndex = 0;
    tcpServerIndexMap_.clear();

    for (int modIndex = 0; modIndex < serverModuleMgr_.getCount(); modIndex++)
    {
        IseServerModule& serverModule = serverModuleMgr_.getItem(modIndex);
        for (int serverIndex = 0; serverIndex < serverModule.getTcpServerCount(); serverIndex++)
        {
            tcpServerIndexMap_[globalServerIndex] = modIndex;
            globalServerIndex++;
        }
    }
}

//-----------------------------------------------------------------------------
// 描述: 更新ISE配置
//-----------------------------------------------------------------------------
void IseSvrModBusiness::updateIseOptions()
{
    IseOptions& options = iseApp().iseOptions();

    // 设置UDP请求组别的总数量
    options.setUdpRequestGroupCount(getUdpGroupCount());
    // 设置TCP服务器的总数量
    options.setTcpServerCount(getTcpServerCount());

    // UDP请求组别相关设置
    int globalGroupIndex = 0;
    for (int modIndex = 0; modIndex < serverModuleMgr_.getCount(); modIndex++)
    {
        IseServerModule& serverModule = serverModuleMgr_.getItem(modIndex);
        for (int groupIndex = 0; groupIndex < serverModule.getUdpGroupCount(); groupIndex++)
        {
            UdpGroupOptions grpOpt;
            serverModule.getUdpGroupOptions(groupIndex, grpOpt);
            options.setUdpRequestQueueCapacity(globalGroupIndex, grpOpt.requestQueueCapacity);
            options.setUdpWorkerThreadCount(globalGroupIndex, grpOpt.minWorkerThreads, grpOpt.maxWorkerThreads);
            globalGroupIndex++;
        }
    }

    // TCP服务器相关设置
    int globalServerIndex = 0;
    for (int modIndex = 0; modIndex < serverModuleMgr_.getCount(); modIndex++)
    {
        IseServerModule& serverModule = serverModuleMgr_.getItem(modIndex);
        for (int serverIndex = 0; serverIndex < serverModule.getTcpServerCount(); serverIndex++)
        {
            TcpServerOptions svrOpt;
            serverModule.getTcpServerOptions(serverIndex, svrOpt);
            options.setTcpServerPort(globalServerIndex, svrOpt.serverPort);
            options.setTcpServerEventLoopCount(globalServerIndex, svrOpt.eventLoopCount);
            globalServerIndex++;
        }
    }

    // 设置辅助服务线程的数量
    int assistorCount = 0;
    for (int i = 0; i < serverModuleMgr_.getCount(); i++)
        assistorCount += serverModuleMgr_.getItem(i).getAssistorThreadCount();
    options.setAssistorThreadCount(assistorCount);
}

///////////////////////////////////////////////////////////////////////////////

} // namespace ise
