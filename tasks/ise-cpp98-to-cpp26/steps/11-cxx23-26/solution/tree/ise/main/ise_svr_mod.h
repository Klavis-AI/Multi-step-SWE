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
// ise_svr_mod.h
///////////////////////////////////////////////////////////////////////////////

#ifndef _ISE_SVR_MOD_H_
#define _ISE_SVR_MOD_H_

#include "ise/main/ise_options.h"
#include "ise/main/ise_application.h"
#include "ise/main/ise_svr_mod_msgs.h"

#include <flat_map>

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// 提前声明

class IseServerModule;
class IseServerModuleMgr;
class IseSvrModBusiness;

///////////////////////////////////////////////////////////////////////////////
// 类型定义

// 动作代码数组
typedef std::vector<UINT> ActionCodeArray;

typedef IseOptions::UdpRequestGroupOption UdpGroupOptions;
typedef IseOptions::TcpServerOption TcpServerOptions;

// 服务模块由模块管理器持有。业务对象仍然用一个裸指针列表把它们交出去，
// 交出之前每个模块都在句柄里 —— 句柄只在交接的那一刻松手。
typedef std::unique_ptr<IseServerModule> IseServerModuleHandle;
typedef std::vector<IseServerModule*> IseServerModuleList;

///////////////////////////////////////////////////////////////////////////////
// class IseServerModule - 服务器模块基类

class IseServerModule : boost::noncopyable
{
public:
    friend class IseServerModuleMgr;
public:
    IseServerModule() : svrModIndex_(0) {}
    virtual ~IseServerModule() {}

    // 取得该服务模块中的UDP组别数量
    virtual int getUdpGroupCount() { return 0; }
    // 取得该模块中某UDP组别所接管的动作代码
    virtual void getUdpGroupActionCodes(int /*groupIndex*/, ActionCodeArray& /*list*/) {}
    // 取得该模块中某UDP组别的配置
    virtual void getUdpGroupOptions(int /*groupIndex*/, UdpGroupOptions& /*options*/) {}
    // 取得该服务模块中的TCP服务器数量
    virtual int getTcpServerCount() { return 0; }
    // 取得该模块中某TCP服务器的配置
    virtual void getTcpServerOptions(int /*serverIndex*/, TcpServerOptions& /*options*/) {}

    // 收到了UDP数据包
    virtual void onRecvedUdpPacket(UdpWorkerThread& /*workerThread*/, UdpPacket& /*packet*/) {}

    // 接受了一个新的TCP连接: 整条会话写在这个协程里
    virtual ConnectionTask onTcpConnection(TcpConnectionPtr /*connection*/) { co_return; }
    // 断开了一个TCP连接
    virtual void onTcpDisconnected(const TcpConnectionPtr& /*connection*/) {}

    // 返回此模块所需辅助服务线程的数量
    virtual int getAssistorThreadCount() { return 0; }
    // 辅助服务线程执行(assistorIndex: 0-based)
    virtual void assistorThreadExecute(AssistorThread& /*assistorThread*/, int /*assistorIndex*/) {}
    // 系统守护线程执行 (secondCount: 0-based)
    virtual void daemonThreadExecute(Thread& /*thread*/, int /*secondCount*/) {}

    // 收到服务模块消息
    virtual void onSvrModMessage(BaseSvrModMessage& /*message*/) {}

    // 返回服务模块序号
    int getSvrModIndex() const { return svrModIndex_; }

    // 向所有服务模块广播消息
    void broadcastMessage(BaseSvrModMessage& message);

private:
    int svrModIndex_;   // (0-based)
};

///////////////////////////////////////////////////////////////////////////////
// class IseServerModuleMgr - 服务模块管理器

class IseServerModuleMgr : boost::noncopyable
{
public:
    IseServerModuleMgr();
    virtual ~IseServerModuleMgr();

    void initServerModuleList(const IseServerModuleList& list);
    void clearServerModuleList();

    inline int getCount() const { return static_cast<int>(items_.size()); }
    inline IseServerModule& getItem(int index) { return *items_[index]; }

private:
    std::vector<IseServerModuleHandle> items_;   // 服务模块列表，列表持有模块的所有权
};

///////////////////////////////////////////////////////////////////////////////
// class IseSvrModBusiness - 支持服务模块的ISE业务类

class IseSvrModBusiness : public IseBusiness
{
public:
    friend class IseServerModule;

public:
    IseSvrModBusiness() {}
    ~IseSvrModBusiness() override {}
public:
    void initialize() override;
    void finalize() override;

    void classifyUdpPacket(std::span<const std::byte> packet, int& groupIndex) override;
    void onRecvedUdpPacket(UdpWorkerThread& workerThread, int groupIndex, UdpPacket& packet) override;

    ConnectionTask onTcpConnection(TcpConnectionPtr connection) override;
    void onTcpDisconnected(const TcpConnectionPtr& connection) override;

    void assistorThreadExecute(AssistorThread& assistorThread, int assistorIndex) override;
    void daemonThreadExecute(Thread& thread, int secondCount) override;

public:
    int getAssistorIndex(int serverModuleIndex, int localAssistorIndex);
    void broadcastMessage(BaseSvrModMessage& message);

protected:
    // UDP数据包过滤函数 (返回: true-有效包, false-无效包)
    virtual bool filterUdpPacket(std::span<const std::byte> /*packet*/) { return true; }
    // 取得UDP数据包中的动作代码
    virtual UINT getUdpPacketActionCode(std::span<const std::byte> /*packet*/) { return 0; }
    // 创建所有服务模块
    virtual void createServerModules(IseServerModuleList& /*svrModList*/) {}

private:
    int getUdpGroupCount();
    int getTcpServerCount();
    void initActionCodeMap();
    void initUdpGroupIndexMap();
    void initTcpServerIndexMap();
    void updateIseOptions();

protected:
    // 三张映射表都在服务模块初始化时一次建好, 之后只查不改: 有序连续存储的
    // std::flat_map 正是为这种用法准备的。
    typedef std::flat_map<UINT, int> ActionCodeMap;       // <动作代码, UDP组别号>
    typedef std::flat_map<UINT, int> UdpGroupIndexMap;    // <全局UDP组别号, 服务模块号>
    typedef std::flat_map<UINT, int> TcpServerIndexMap;   // <全局TCP服务器序号, 服务模块号>

    IseServerModuleMgr serverModuleMgr_;             // 服务模块管理器
    ActionCodeMap actionCodeMap_;                    // <动作代码, UDP组别号> 映射表
    UdpGroupIndexMap udpGroupIndexMap_;              // <全局UDP组别号, 服务模块号> 映射表
    TcpServerIndexMap tcpServerIndexMap_;            // <全局TCP服务器序号, 服务模块号> 映射表
};

///////////////////////////////////////////////////////////////////////////////

} // namespace ise

#endif // _ISE_SVR_MOD_H_
