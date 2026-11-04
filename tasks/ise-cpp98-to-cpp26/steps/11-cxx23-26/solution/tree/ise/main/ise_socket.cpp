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
// 文件名称: ise_socket.cpp
// 功能描述: 网络基础类库
///////////////////////////////////////////////////////////////////////////////

// 配置层: 宏不跨模块, 这一层永远是文本包含。
#include "ise/main/ise_options.h"
#include "ise/main/ise_global_defs.h"
#include "ise/main/ise_stldefs.h"
#include "ise/main/ise_assert.h"

// 这些平台声明以前是随项目头文件一起进来的。
#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <string>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdarg>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string_view>
#include <type_traits>
#include <sys/timeb.h>
#include <fstream>
#include <stdint.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/file.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <algorithm>
#include <compare>
#include <ranges>
#include <span>
#include <pthread.h>
#include <system_error>
#include <thread>
#include <concepts>
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <deque>

import ise.socket;
import ise.sysutils;
import ise.exceptions;

#ifdef ISE_COMPILER_VC
#pragma comment(lib, "ws2_32.lib")
#endif

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// 杂项函数

static int s_networkInitCount = 0;

//-----------------------------------------------------------------------------
// 描述: 网络初始化 (若失败则抛出异常)
//-----------------------------------------------------------------------------
void networkInitialize()
{
    s_networkInitCount++;
    if (s_networkInitCount > 1) return;

#ifdef ISE_WINDOWS
    WSAData wsd;
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
    {
        s_networkInitCount--;
        iseThrowSocketLastError();
    }
#endif
}

//-----------------------------------------------------------------------------
// 描述: 网络结束化
//-----------------------------------------------------------------------------
void networkFinalize()
{
    if (s_networkInitCount > 0)
        s_networkInitCount--;
    if (s_networkInitCount != 0) return;

#ifdef ISE_WINDOWS
    WSACleanup();
#endif
}

//-----------------------------------------------------------------------------

bool isNetworkInited()
{
    return (s_networkInitCount > 0);
}

//-----------------------------------------------------------------------------

void ensureNetworkInited()
{
    if (!isNetworkInited())
        networkInitialize();
}

//-----------------------------------------------------------------------------
// 描述: 取得最后的错误代码
//-----------------------------------------------------------------------------
int iseSocketGetLastError()
{
#ifdef ISE_WINDOWS
    return WSAGetLastError();
#endif
#ifdef ISE_LINUX
    return errno;
#endif
}

//-----------------------------------------------------------------------------
// 描述: 返回错误信息
//-----------------------------------------------------------------------------
string iseSocketGetErrorMsg(int errorCode)
{
    string result;
    const char *p = "";

    switch (errorCode)
    {
    case SS_EINTR:              p = SSEM_EINTR;                break;
    case SS_EBADF:              p = SSEM_EBADF;                break;
    case SS_EACCES:             p = SSEM_EACCES;               break;
    case SS_EFAULT:             p = SSEM_EFAULT;               break;
    case SS_EINVAL:             p = SSEM_EINVAL;               break;
    case SS_EMFILE:             p = SSEM_EMFILE;               break;

    case SS_EWOULDBLOCK:        p = SSEM_EWOULDBLOCK;          break;
    case SS_EINPROGRESS:        p = SSEM_EINPROGRESS;          break;
    case SS_EALREADY:           p = SSEM_EALREADY;             break;
    case SS_ENOTSOCK:           p = SSEM_ENOTSOCK;             break;
    case SS_EDESTADDRREQ:       p = SSEM_EDESTADDRREQ;         break;
    case SS_EMSGSIZE:           p = SSEM_EMSGSIZE;             break;
    case SS_EPROTOTYPE:         p = SSEM_EPROTOTYPE;           break;
    case SS_ENOPROTOOPT:        p = SSEM_ENOPROTOOPT;          break;
    case SS_EPROTONOSUPPORT:    p = SSEM_EPROTONOSUPPORT;      break;
    case SS_ESOCKTNOSUPPORT:    p = SSEM_ESOCKTNOSUPPORT;      break;
    case SS_EOPNOTSUPP:         p = SSEM_EOPNOTSUPP;           break;
    case SS_EPFNOSUPPORT:       p = SSEM_EPFNOSUPPORT;         break;
    case SS_EAFNOSUPPORT:       p = SSEM_EAFNOSUPPORT;         break;
    case SS_EADDRINUSE:         p = SSEM_EADDRINUSE;           break;
    case SS_EADDRNOTAVAIL:      p = SSEM_EADDRNOTAVAIL;        break;
    case SS_ENETDOWN:           p = SSEM_ENETDOWN;             break;
    case SS_ENETUNREACH:        p = SSEM_ENETUNREACH;          break;
    case SS_ENETRESET:          p = SSEM_ENETRESET;            break;
    case SS_ECONNABORTED:       p = SSEM_ECONNABORTED;         break;
    case SS_ECONNRESET:         p = SSEM_ECONNRESET;           break;
    case SS_ENOBUFS:            p = SSEM_ENOBUFS;              break;
    case SS_EISCONN:            p = SSEM_EISCONN;              break;
    case SS_ENOTCONN:           p = SSEM_ENOTCONN;             break;
    case SS_ESHUTDOWN:          p = SSEM_ESHUTDOWN;            break;
    case SS_ETOOMANYREFS:       p = SSEM_ETOOMANYREFS;         break;
    case SS_ETIMEDOUT:          p = SSEM_ETIMEDOUT;            break;
    case SS_ECONNREFUSED:       p = SSEM_ECONNREFUSED;         break;
    case SS_ELOOP:              p = SSEM_ELOOP;                break;
    case SS_ENAMETOOLONG:       p = SSEM_ENAMETOOLONG;         break;
    case SS_EHOSTDOWN:          p = SSEM_EHOSTDOWN;            break;
    case SS_EHOSTUNREACH:       p = SSEM_EHOSTUNREACH;         break;
    case SS_ENOTEMPTY:          p = SSEM_ENOTEMPTY;            break;
    }

    result = formatString(SSEM_ERROR, errorCode, p);
    return result;
}

//-----------------------------------------------------------------------------
// 描述: 取得最后错误的对应信息
//-----------------------------------------------------------------------------
string iseSocketGetLastErrMsg()
{
    return iseSocketGetErrorMsg(iseSocketGetLastError());
}

//-----------------------------------------------------------------------------
// 描述: 关闭套接字
//-----------------------------------------------------------------------------
void iseCloseSocket(SOCKET handle)
{
#ifdef ISE_WINDOWS
    closesocket(handle);
#endif
#ifdef ISE_LINUX
    close(handle);
#endif
}

//-----------------------------------------------------------------------------
// 描述: 整形IP(主机字节顺序) -> 串型IP
//-----------------------------------------------------------------------------
string ipToString(UINT ip)
{
#pragma pack(1)
    union CIpUnion
    {
        UINT value;
        struct
        {
            unsigned char ch1;  // value的最低字节
            unsigned char ch2;
            unsigned char ch3;
            unsigned char ch4;
        } Bytes;
    } IpUnion;
#pragma pack()
    IpUnion.value = ip;
    // 四个字节拼成一个地址。从前这里有一个 64 字节的栈数组, 它的长度和
    // 格式串之间没有任何联系; 现在结果自己决定自己有多长。
    return std::format("{}.{}.{}.{}", IpUnion.Bytes.ch4, IpUnion.Bytes.ch3,
        IpUnion.Bytes.ch2, IpUnion.Bytes.ch1);
}

//-----------------------------------------------------------------------------
// 描述: 串型IP -> 整形IP(主机字节顺序)
//-----------------------------------------------------------------------------
UINT stringToIp(const string& str)
{
#pragma pack(1)
    union CIpUnion
    {
        UINT value;
        struct
        {
            unsigned char ch1;
            unsigned char ch2;
            unsigned char ch3;
            unsigned char ch4;
        } Bytes;
    } ipUnion;
#pragma pack()
    IntegerArray intList;

    splitStringToInt(str, '.', intList);
    if (intList.size() == 4)
    {
        ipUnion.Bytes.ch1 = static_cast<unsigned char>(intList[3]);
        ipUnion.Bytes.ch2 = static_cast<unsigned char>(intList[2]);
        ipUnion.Bytes.ch3 = static_cast<unsigned char>(intList[1]);
        ipUnion.Bytes.ch4 = static_cast<unsigned char>(intList[0]);
        return ipUnion.value;
    }
    else
        return 0;
}

//-----------------------------------------------------------------------------
// 描述: 取得套接字的“本地地址”
//-----------------------------------------------------------------------------
InetAddress getSocketLocalAddr(SOCKET handle)
{
    SockAddr localAddr;

    memset(&localAddr, 0, sizeof(localAddr));
    socklen_t addrLen = sizeof(localAddr);
    if (::getsockname(handle, (struct sockaddr*)&localAddr, &addrLen) < 0)
        memset(&localAddr, 0, sizeof(localAddr));

    return InetAddress(localAddr);
}

//-----------------------------------------------------------------------------
// 描述: 取得套接字的“对端地址”
//-----------------------------------------------------------------------------
InetAddress getSocketPeerAddr(SOCKET handle)
{
    SockAddr peerAddr;

    memset(&peerAddr, 0, sizeof(peerAddr));
    socklen_t addrLen = sizeof(peerAddr);
    if (::getpeername(handle, (struct sockaddr*)&peerAddr, &addrLen) < 0)
        memset(&peerAddr, 0, sizeof(peerAddr));

    return InetAddress(peerAddr);
}

//-----------------------------------------------------------------------------
// 描述: 取得空闲端口号
// 参数:
//   proto      - 网络协议(UDP,TCP)
//   startPort  - 起始端口号
//   checkTimes - 检测次数
// 返回:
//   空闲端口号 (若失败则返回 0)
//-----------------------------------------------------------------------------
int getFreePort(NET_PROTO_TYPE proto, int startPort, int checkTimes)
{
    int i, result = 0;
    bool success;
    SockAddr addr;

    ise::networkInitialize();
    AutoFinalizer finalizer(&ise::networkFinalize);

    SOCKET s = socket(PF_INET, (proto == NET_PROTO_TYPE::NPT_UDP? SOCK_DGRAM : SOCK_STREAM), IPPROTO_IP);
    if (s == INVALID_SOCKET) return 0;

    success = false;
    for (i = 0; i < checkTimes; i++)
    {
        result = startPort + i;
        addr = InetAddress(ntohl(INADDR_ANY), static_cast<WORD>(result)).getSockAddr();
        if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != -1)
        {
            success = true;
            break;
        }
    }

    iseCloseSocket(s);
    if (!success) result = 0;
    return result;
}

//-----------------------------------------------------------------------------
// 描述: 取得本机IP列表
//-----------------------------------------------------------------------------
void getLocalIpList(StrList& ipList)
{
#ifdef ISE_WINDOWS
    char hostName[250];
    hostent *hostEnt;
    in_addr **addrPtr;

    ipList.clear();
    gethostname(hostName, sizeof(hostName));
    hostEnt = gethostbyname(hostName);
    if (hostEnt)
    {
        addrPtr = (in_addr**)(hostEnt->h_addr_list);
        int i = 0;
        while (addrPtr[i])
        {
            UINT ip = ntohl( *(UINT*)(addrPtr[i]) );
            ipList.add(ipToString(ip));
            i++;
        }
    }
#endif
#ifdef ISE_LINUX
    const int MAX_INTERFACES = 16;
    int fd, intfCount;
    struct ifreq buf[MAX_INTERFACES];
    struct ifconf ifc;

    ipList.clear();
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0)
    {
        ifc.ifc_len = sizeof(buf);
        ifc.ifc_buf = (caddr_t) buf;
        if (!ioctl(fd, SIOCGIFCONF, (char*)&ifc))
        {
            intfCount = ifc.ifc_len / sizeof(struct ifreq);
            for (int i = 0; i < intfCount; i++)
            {
                ioctl(fd, SIOCGIFADDR, (char*)&buf[i]);
                UINT nIp = ((struct sockaddr_in*)(&buf[i].ifr_addr))->sin_addr.s_addr;
                ipList.add(ipToString(ntohl(nIp)));
            }
        }
        close(fd);
    }
#endif
}

//-----------------------------------------------------------------------------
// 描述: 取得本机IP
//-----------------------------------------------------------------------------
string getLocalIp()
{
    StrList ipList;
    string result;

    getLocalIpList(ipList);
    if (!ipList.isEmpty())
    {
        if (ipList.getCount() == 1)
            result = ipList[0];
        else
        {
            for (int i = 0; i < ipList.getCount(); i++)
            {
                if (ipList[i] != "127.0.0.1")
                {
                    result = ipList[i];
                    break;
                }
            }

            if (result.length() == 0)
                result = ipList[0];
        }
    }

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 域名地址 -> IP地址
// 备注: 若失败，则返回空字符串。
//-----------------------------------------------------------------------------
string lookupHostAddr(const string& host)
{
    string result = "";

    struct hostent* hostentPtr = gethostbyname(host.c_str());
    if (hostentPtr != nullptr)
        result = ipToString(ntohl(((struct in_addr *)hostentPtr->h_addr)->s_addr));

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 取最后的错误码并抛出异常
//-----------------------------------------------------------------------------
void iseThrowSocketLastError()
{
    iseThrowSocketException(iseSocketGetLastErrMsg().c_str());
}

///////////////////////////////////////////////////////////////////////////////
// class InetAddress

string InetAddress::getDisplayStr() const
{
    return std::format("{}:{}", ipToString(ip), port);
}

///////////////////////////////////////////////////////////////////////////////
// class Socket

Socket::Socket() :
    isActive_(false),
    handle_(INVALID_SOCKET),
    domain_(PF_INET),
    type_(SOCK_STREAM),
    protocol_(IPPROTO_IP),
    isBlockMode_(true)
{
    // nothing
}

//-----------------------------------------------------------------------------

Socket::~Socket()
{
    close();
}

//-----------------------------------------------------------------------------
// 描述: 打开套接字
//-----------------------------------------------------------------------------
void Socket::open()
{
    if (!isActive_)
    {
        try
        {
            SOCKET handle;
            handle = socket(domain_, type_, protocol_);
            if (handle == INVALID_SOCKET)
                iseThrowSocketLastError();
            isActive_ = (handle != INVALID_SOCKET);
            if (isActive_)
            {
                handle_ = handle;
                setBlockMode(isBlockMode_);
            }
        }
        catch (SocketException&)
        {
            doClose();
            throw;
        }
    }
}

//-----------------------------------------------------------------------------
// 描述: 关闭套接字
//-----------------------------------------------------------------------------
void Socket::close()
{
    if (isActive_) doClose();
}

//-----------------------------------------------------------------------------

InetAddress Socket::getLocalAddr() const
{
    return getSocketLocalAddr(handle_);
}

//-----------------------------------------------------------------------------

InetAddress Socket::getPeerAddr() const
{
    return getSocketPeerAddr(handle_);
}

//-----------------------------------------------------------------------------

void Socket::setBlockMode(bool value)
{
    // 此处不应作 value != isBlockMode_ 的判断，因为在不同的平台下，
    // 套接字阻塞方式的缺省值不一样。
    if (isActive_)
        doSetBlockMode(handle_, value);
    isBlockMode_ = value;
}

//-----------------------------------------------------------------------------

void Socket::setHandle(SOCKET value)
{
    if (value != handle_)
    {
        if (isActive()) close();
        handle_ = value;
        if (handle_ != INVALID_SOCKET)
            isActive_ = true;
    }
}

//-----------------------------------------------------------------------------

void Socket::setActive(bool value)
{
    if (value != isActive_)
    {
        if (value) open();
        else close();
    }
}

//-----------------------------------------------------------------------------

void Socket::setDomain(int value)
{
    if (value != domain_)
    {
        if (isActive()) close();
        domain_ = value;
    }
}

//-----------------------------------------------------------------------------

void Socket::setType(int value)
{
    if (value != type_)
    {
        if (isActive()) close();
        type_ = value;
    }
}

//-----------------------------------------------------------------------------

void Socket::setProtocol(int value)
{
    if (value != protocol_)
    {
        if (isActive()) close();
        protocol_ = value;
    }
}

//-----------------------------------------------------------------------------
// 描述: 绑定套接字
//-----------------------------------------------------------------------------
void Socket::bind(WORD port)
{
    SockAddr addr = InetAddress(ntohl(INADDR_ANY), port).getSockAddr();
    int optVal = 1;

    // 强制重新绑定，而不受其它因素的影响
    setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, (char*)&optVal, sizeof(optVal));

    // 绑定套接字
    if (::bind(handle_, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        iseThrowSocketLastError();
}

//-----------------------------------------------------------------------------

void Socket::doSetBlockMode(SOCKET handle, bool value)
{
#ifdef ISE_WINDOWS
    UINT notBlock = (value? 0 : 1);
    if (ioctlsocket(handle, FIONBIO, (u_long*)&notBlock) < 0)
        iseThrowSocketLastError();
#endif
#ifdef ISE_LINUX
    int flag = fcntl(handle, F_GETFL);

    if (value)
        flag &= ~O_NONBLOCK;
    else
        flag |= O_NONBLOCK;

    if (fcntl(handle, F_SETFL, flag) < 0)
        iseThrowSocketLastError();
#endif
}

//-----------------------------------------------------------------------------
// 备注:
//   在 Winsock 中，关闭接收通道 (SS_SD_RECV 或 SS_SD_BOTH) 后，如果当前接收缓
//   存中仍有未取出数据或者之后再有数据到达，TCP会向发送端发送RST包，导致连接
//   被重置。而 Linux 下的行为有所不同，不会重置连接。
//-----------------------------------------------------------------------------
void Socket::doClose()
{
#ifdef ISE_WINDOWS
    ::shutdown(handle_, SS_SD_SEND);
#endif
#ifdef ISE_LINUX
    ::shutdown(handle_, SS_SD_BOTH);
#endif

    iseCloseSocket(handle_);
    handle_ = INVALID_SOCKET;
    isActive_ = false;
}

///////////////////////////////////////////////////////////////////////////////
// class UdpSocket

//-----------------------------------------------------------------------------
// 描述: 接收数据
//-----------------------------------------------------------------------------
int UdpSocket::recvBuffer(std::span<std::byte> buffer)
{
    InetAddress peerAddr;
    return recvBuffer(buffer, peerAddr);
}

//-----------------------------------------------------------------------------
// 描述: 接收数据
//-----------------------------------------------------------------------------
int UdpSocket::recvBuffer(std::span<std::byte> buffer, InetAddress& peerAddr)
{
    SockAddr addr;
    int bytes;
    socklen_t sockLen = sizeof(addr);

    memset(&addr, 0, sizeof(addr));
    bytes = recvfrom(handle_, reinterpret_cast<char*>(buffer.data()), buffer.size(), 0,
        (struct sockaddr*)&addr, &sockLen);

    if (bytes > 0)
    {
        peerAddr.ip = ntohl(addr.sin_addr.s_addr);
        peerAddr.port = ntohs(addr.sin_port);
    }

    return bytes;
}

//-----------------------------------------------------------------------------
// 描述: 发送数据
//-----------------------------------------------------------------------------
int UdpSocket::sendBuffer(std::span<const std::byte> data, const InetAddress& peerAddr, int sendTimes)
{
    int result = 0;
    SockAddr addr = peerAddr.getSockAddr();
    socklen_t sockLen = sizeof(addr);

    for (int i = 0; i < sendTimes; i++)
        result = sendto(handle_, reinterpret_cast<const char*>(data.data()), data.size(), 0,
            (struct sockaddr*)&addr, sockLen);

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 打开套接字
//-----------------------------------------------------------------------------
void UdpSocket::open()
{
    Socket::open();

#ifdef ISE_WINDOWS
    if (isActive_)
    {
        // Windows下，当收到ICMP包("ICMP port unreachable")后，recvfrom将返回-1，
        // 错误为 WSAECONNRESET(10054)。用下面的方法禁用该行为。

        #define IOC_VENDOR        0x18000000
        #define _WSAIOW(x,y)      (IOC_IN|(x)|(y))
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)

        DWORD bytesReturned = 0;
        BOOL newBehavior = FALSE;
        ::WSAIoctl(getHandle(), SIO_UDP_CONNRESET, &newBehavior, sizeof(newBehavior),
            NULL, 0, &bytesReturned, NULL, NULL);
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////
// class UdpServer

BaseUdpServer::BaseUdpServer() :
    localPort_(0),
    listenerThreadPool_(new UdpListenerThreadPool(this))
{
    setListenerThreadCount(1);
}

//-----------------------------------------------------------------------------

BaseUdpServer::~BaseUdpServer()
{
    // nothing
}

//-----------------------------------------------------------------------------
// 描述: 设置监听端口
//-----------------------------------------------------------------------------
void BaseUdpServer::setLocalPort(WORD value)
{
    if (value != localPort_)
    {
        if (isActive()) close();
        localPort_ = value;
    }
}

//-----------------------------------------------------------------------------
// 描述: 开启 UDP 服务器
//-----------------------------------------------------------------------------
void BaseUdpServer::open()
{
    try
    {
        if (!isActive_)
        {
            UdpSocket::open();
            if (isActive_)
            {
                bind(localPort_);
                startListenerThreads();
            }
        }
    }
    catch (SocketException&)
    {
        close();
        throw;
    }
}

//-----------------------------------------------------------------------------
// 描述: 关闭 UDP 服务器
//-----------------------------------------------------------------------------
void BaseUdpServer::close()
{
    if (isActive())
    {
        stopListenerThreads();
        UdpSocket::close();
    }
}

//-----------------------------------------------------------------------------
// 描述: 取得监听线程的数量
//-----------------------------------------------------------------------------
int BaseUdpServer::getListenerThreadCount() const
{
    return listenerThreadPool_->getMaxThreadCount();
}

//-----------------------------------------------------------------------------
// 描述: 设置监听线程的数量
//-----------------------------------------------------------------------------
void BaseUdpServer::setListenerThreadCount(int value)
{
    if (value < 1) value = 1;
    listenerThreadPool_->setMaxThreadCount(value);
}

//-----------------------------------------------------------------------------
// 描述: 设置“收到数据包”的回调
//-----------------------------------------------------------------------------
void BaseUdpServer::setRecvDataCallback(const UdpSvrRecvDataCallback& callback)
{
    onRecvData_ = callback;
}

//-----------------------------------------------------------------------------
// 描述: 启动监听线程
//-----------------------------------------------------------------------------
void BaseUdpServer::startListenerThreads()
{
    listenerThreadPool_->startThreads();
}

//-----------------------------------------------------------------------------
// 描述: 停止监听线程
//-----------------------------------------------------------------------------
void BaseUdpServer::stopListenerThreads()
{
    listenerThreadPool_->stopThreads();
}

//-----------------------------------------------------------------------------
// 描述: 收到数据包
//-----------------------------------------------------------------------------
void BaseUdpServer::dataReceived(std::span<const std::byte> packet, const InetAddress& peerAddr)
{
    if (onRecvData_)
        onRecvData_(packet, peerAddr);
}

///////////////////////////////////////////////////////////////////////////////
// class TcpSocket

//-----------------------------------------------------------------------------
// 描述: shutdown 操作
//-----------------------------------------------------------------------------
void TcpSocket::shutdown(bool closeSend, bool closeRecv)
{
    if (isActive_ && (closeSend || closeRecv))
    {
        int how;
        if (closeSend && !closeRecv)
            how = SS_SD_SEND;
        else if (!closeSend && closeRecv)
            how = SS_SD_RECV;
        else
            how = SS_SD_BOTH;   // the enclosing if has already ruled out
                                // neither, so this is the both case

        ::shutdown(handle_, how);
    }
}

///////////////////////////////////////////////////////////////////////////////
// class BaseTcpConnection

BaseTcpConnection::BaseTcpConnection() :
    isDisconnected_(false)
{
    socket_.setBlockMode(false);
}

//-----------------------------------------------------------------------------

BaseTcpConnection::BaseTcpConnection(SOCKET socketHandle) :
    isDisconnected_(false)
{
    socket_.setHandle(socketHandle);
    socket_.setBlockMode(false);
}

//-----------------------------------------------------------------------------
// 描述: 返回当前是否为连接状态
//-----------------------------------------------------------------------------
bool BaseTcpConnection::isConnected() const
{
    return socket_.isActive() && !isDisconnected_;
}

//-----------------------------------------------------------------------------
// 描述: 断开连接
//-----------------------------------------------------------------------------
void BaseTcpConnection::disconnect()
{
    if (isConnected())
        doDisconnect();
    isDisconnected_ = true;
}

//-----------------------------------------------------------------------------
// 描述: 执行 shutdown 操作
//-----------------------------------------------------------------------------
void BaseTcpConnection::shutdown(bool closeSend, bool closeRecv)
{
    socket_.shutdown(closeSend, closeRecv);
}

//-----------------------------------------------------------------------------
// 描述: 设置 TCP_NODELAY 标志
//-----------------------------------------------------------------------------
void BaseTcpConnection::setNoDelay(bool value)
{
    int optVal = value ? 1 : 0;
    ::setsockopt(getSocket().getHandle(), IPPROTO_TCP, TCP_NODELAY,
        (char*)&optVal, sizeof(optVal));
}

//-----------------------------------------------------------------------------
// 描述: 设置 SO_KEEPALIVE 标志
//-----------------------------------------------------------------------------
void BaseTcpConnection::setKeepAlive(bool value)
{
    int optVal = value ? 1 : 0;
    ::setsockopt(getSocket().getHandle(), IPPROTO_TCP, SO_KEEPALIVE,
        (char*)&optVal, sizeof(optVal));
}

//-----------------------------------------------------------------------------
// 描述: 取得此连接的本地地址
//-----------------------------------------------------------------------------
const InetAddress& BaseTcpConnection::getLocalAddr() const
{
    if (localAddr_.isEmpty())
        localAddr_ = getSocket().getLocalAddr();
    return localAddr_;
}

//-----------------------------------------------------------------------------
// 描述: 取得此连接的对方地址
//-----------------------------------------------------------------------------
const InetAddress& BaseTcpConnection::getPeerAddr() const
{
    if (peerAddr_.isEmpty())
        peerAddr_ = getSocket().getPeerAddr();
    return peerAddr_;
}

//-----------------------------------------------------------------------------
// 描述: 断开连接
//-----------------------------------------------------------------------------
void BaseTcpConnection::doDisconnect()
{
    socket_.close();
}

//-----------------------------------------------------------------------------
// 描述: 发送数据
//   syncMode     - 是否以同步方式发送
//   timeoutMSecs - 指定超时时间(毫秒)，若超过指定时间仍未发送完全部数据则退出函数。
//                   若 timeoutMSecs 为 -1，则表示不进行超时检测。
// 返回:
//   < 0    - 未发出任何数据，且发送数据过程发生了错误。
//   >= 0   - 实际发出的字节数。
// 备注:
//   1. 不会抛出异常。
//   2. 若仅因为超时而返回，返回值不会小于0。
//-----------------------------------------------------------------------------
int BaseTcpConnection::sendBuffer(std::span<const std::byte> data, bool syncMode, int timeoutMSecs)
{
    int size = static_cast<int>(data.size());
    int result = size;

    if (syncMode)
        result = doSyncSendBuffer(data.data(), size, timeoutMSecs);
    else
        result = doAsyncSendBuffer(data.data(), size);

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 接收数据
//   syncMode     - 是否以同步方式接收
//   timeoutMSecs - 指定超时时间(毫秒)，若超过指定时间仍未接收完全部数据则退出函数。
//                   若 timeoutMSecs 为 -1，则表示不进行超时检测。
// 返回:
//   < 0    - 未接收到任何数据，且接收数据过程发生了错误。
//   >= 0   - 实际接收到的字节数。
// 备注:
//   1. 不会抛出异常。
//   2. 若仅因为超时而返回，返回值不会小于0。
//-----------------------------------------------------------------------------
int BaseTcpConnection::recvBuffer(std::span<std::byte> buffer, bool syncMode, int timeoutMSecs)
{
    int size = static_cast<int>(buffer.size());
    int result = size;

    if (syncMode)
        result = doSyncRecvBuffer(buffer.data(), size, timeoutMSecs);
    else
        result = doAsyncRecvBuffer(buffer.data(), size);

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 发送数据
//   timeoutMSecs - 指定超时时间(毫秒)，若超过指定时间仍未发送完全部数据则退出函数。
//                   若 timeoutMSecs 为 -1，则表示不进行超时检测。
// 返回:
//   < 0    - 未发出任何数据，且发送数据过程发生了错误。
//   >= 0   - 实际发出的字节数。
// 备注:
//   1. 不会抛出异常。
//   2. 若仅因为超时而返回，返回值不会小于0。
//   3. 此处采用非阻塞套接字模式，以便能及时退出。
//-----------------------------------------------------------------------------
int BaseTcpConnection::doSyncSendBuffer(const void *buffer, int size, int timeoutMSecs)
{
    const int SELECT_WAIT_MSEC = 250;    // 每次等待时间 (毫秒)

    int result = -1;
    bool error = false;
    fd_set fds;
    struct timeval tv;
    SOCKET socketHandle = socket_.getHandle();
    int n, r, remainSize, index;
    UINT64 startTime, elapsedMSecs;

    if (size <= 0 || !socket_.isActive())
        return result;

    remainSize = size;
    index = 0;
    startTime = getCurTicks();

    while (socket_.isActive() && remainSize > 0)
    try
    {
        tv.tv_sec = 0;
        tv.tv_usec = (timeoutMSecs? SELECT_WAIT_MSEC * 1000 : 0);

        FD_ZERO(&fds);
        FD_SET((UINT)socketHandle, &fds);

        r = select(socketHandle + 1, nullptr, &fds, nullptr, &tv);
        if (r < 0)
        {
            if (iseSocketGetLastError() != SS_EINTR)
            {
                error = true;    // error
                break;
            }
        }

        if (r > 0 && socket_.isActive() && FD_ISSET(socketHandle, &fds))
        {
            n = send(socketHandle, &((const char*)buffer)[index], remainSize, 0);
            if (n <= 0)
            {
                int errorCode = iseSocketGetLastError();
                if ((n == 0) || (errorCode != SS_EWOULDBLOCK && errorCode != SS_EINTR))
                {
                    error = true;    // error
                    break;
                }
                else
                    n = 0;
            }

            index += n;
            remainSize -= n;
        }

        // 如果需要超时检测
        if (timeoutMSecs >= 0 && remainSize > 0)
        {
            elapsedMSecs = getTickDiff(startTime, getCurTicks());
            if (elapsedMSecs >= (UINT64)timeoutMSecs)
                break;
        }
    }
    catch (...)
    {
        error = true;
        break;
    }

    if (index > 0)
        result = index;
    else if (error)
        result = -1;
    else
        result = 0;

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 接收数据
// 参数:
//   timeoutMSecs - 指定超时时间(毫秒)，若超过指定时间仍未接收完全部数据则退出函数。
//                   若 timeoutMSecs 为 -1，则表示不进行超时检测。
// 返回:
//   < 0    - 未接收到任何数据，且接收数据过程发生了错误。
//   >= 0   - 实际接收到的字节数。
// 备注:
//   1. 不会抛出异常。
//   2. 若仅因为超时而返回，返回值不会小于0。
//   3. 此处采用非阻塞套接字模式，以便能及时退出。
//-----------------------------------------------------------------------------
int BaseTcpConnection::doSyncRecvBuffer(void *buffer, int size, int timeoutMSecs)
{
    const int SELECT_WAIT_MSEC = 250;    // 每次等待时间 (毫秒)

    int result = -1;
    bool error = false;
    fd_set fds;
    struct timeval tv;
    SOCKET socketHandle = socket_.getHandle();
    int n, r, remainSize, index;
    UINT64 startTime, elapsedMSecs;

    if (size <= 0 || !socket_.isActive())
        return result;

    remainSize = size;
    index = 0;
    startTime = getCurTicks();

    while (socket_.isActive() && remainSize > 0)
    try
    {
        tv.tv_sec = 0;
        tv.tv_usec = (timeoutMSecs? SELECT_WAIT_MSEC * 1000 : 0);

        FD_ZERO(&fds);
        FD_SET((UINT)socketHandle, &fds);

        r = select(socketHandle + 1, &fds, nullptr, nullptr, &tv);
        if (r < 0)
        {
            if (iseSocketGetLastError() != SS_EINTR)
            {
                error = true;    // error
                break;
            }
        }

        if (r > 0 && socket_.isActive() && FD_ISSET(socketHandle, &fds))
        {
            n = recv(socketHandle, &((char*)buffer)[index], remainSize, 0);
            if (n <= 0)
            {
                int errorCode = iseSocketGetLastError();
                if ((n == 0) || (errorCode != SS_EWOULDBLOCK && errorCode != SS_EINTR))
                {
                    error = true;    // error
                    break;
                }
                else
                    n = 0;
            }

            index += n;
            remainSize -= n;
        }

        // 如果需要超时检测
        if (timeoutMSecs >= 0 && remainSize > 0)
        {
            elapsedMSecs = getTickDiff(startTime, getCurTicks());
            if (elapsedMSecs >= (UINT64)timeoutMSecs)
                break;
        }
    }
    catch (...)
    {
        error = true;
        break;
    }

    if (index > 0)
        result = index;
    else if (error)
        result = -1;
    else
        result = 0;

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 发送数据 (非阻塞)
// 返回:
//   < 0    - 未发送任何数据，且发送数据过程发生了错误。
//   >= 0   - 实际发出的字节数。
// 备注:
//   不会抛出异常。
//-----------------------------------------------------------------------------
int BaseTcpConnection::doAsyncSendBuffer(const void *buffer, int size)
{
    int result = -1;
    try
    {
        result = send(socket_.getHandle(), (const char*)buffer, size, 0);
        if (result <= 0)
        {
            int errorCode = iseSocketGetLastError();
            if ((result == 0) || (errorCode != SS_EWOULDBLOCK && errorCode != SS_EINTR))
                result = -1;   // error
            else
                result = 0;
        }
    }
    catch (...)
    {}

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 接收数据 (非阻塞)
// 返回:
//   < 0    - 未接收到任何数据，且接收数据过程发生了错误。
//   >= 0   - 实际接收到的字节数。
// 备注:
//   不会抛出异常。
//-----------------------------------------------------------------------------
int BaseTcpConnection::doAsyncRecvBuffer(void *buffer, int size)
{
    int result = -1;
    try
    {
        result = recv(socket_.getHandle(), (char*)buffer, size, 0);
        if (result <= 0)
        {
            int errorCode = iseSocketGetLastError();
            if ((result == 0) || (errorCode != SS_EWOULDBLOCK && errorCode != SS_EINTR))
                result = -1;   // error
            else
                result = 0;
        }
    }
    catch (...)
    {}

    return result;
}

///////////////////////////////////////////////////////////////////////////////
// class TcpClient

BaseTcpClient::BaseTcpClient()
{
    // nothing
}

BaseTcpClient::~BaseTcpClient()
{
    // nothing
}

//-----------------------------------------------------------------------------
// 描述: 发起TCP连接请求 (阻塞式)
// 备注: 若连接失败，则抛出异常。
//-----------------------------------------------------------------------------
void BaseTcpClient::connect(const string& ip, int port)
{
    ensureConnCreated();
    TcpSocket& socket = getSocket();

    if (isConnected()) disconnect();

    try
    {
        socket.open();
        if (socket.isActive())
        {
            SockAddr addr = InetAddress(stringToIp(ip), static_cast<WORD>(port)).getSockAddr();

            bool oldBlockMode = socket.isBlockMode();
            socket.setBlockMode(true);

            if (::connect(socket.getHandle(), (struct sockaddr*)&addr, sizeof(addr)) < 0)
                iseThrowSocketLastError();

            socket.setBlockMode(oldBlockMode);
        }
    }
    catch (SocketException&)
    {
        socket.close();
        throw;
    }
}

//-----------------------------------------------------------------------------
// 描述: 发起TCP连接请求 (非阻塞式)
// 参数:
//   timeoutMSecs - 最多等待的毫秒数，为-1表示不等待
// 返回:
//   ACS_CONNECTING - 尚未连接完毕，且尚未发生错误
//   ACS_CONNECTED  - 连接已建立成功
//   ACS_FAILED     - 连接过程中发生了错误，导致连接失败
// 备注:
//   不抛异常。
//-----------------------------------------------------------------------------
int BaseTcpClient::asyncConnect(const string& ip, int port, int timeoutMSecs)
{
    ASYNC_CONNECT_STATE result = ASYNC_CONNECT_STATE::ACS_CONNECTING;

    ensureConnCreated();
    TcpSocket& socket = getSocket();

    if (isConnected()) disconnect();

    try
    {
        socket.open();
        if (socket.isActive())
        {
            SockAddr addr = InetAddress(stringToIp(ip), static_cast<WORD>(port)).getSockAddr();

            socket.setBlockMode(false);
            int r = ::connect(socket.getHandle(), (struct sockaddr*)&addr, sizeof(addr));
            if (r == 0)
                result = ASYNC_CONNECT_STATE::ACS_CONNECTED;
#ifdef ISE_WINDOWS
            else if (iseSocketGetLastError() != SS_EWOULDBLOCK)
#endif
#ifdef ISE_LINUX
            else if (iseSocketGetLastError() != SS_EINPROGRESS)
#endif
                result = ASYNC_CONNECT_STATE::ACS_FAILED;
        }
    }
    catch (...)
    {
        socket.close();
        result = ASYNC_CONNECT_STATE::ACS_FAILED;
    }

    if (result == ASYNC_CONNECT_STATE::ACS_CONNECTING)
        result = static_cast<ASYNC_CONNECT_STATE>(
            checkAsyncConnectState(timeoutMSecs));

    return static_cast<int>(result);
}

//-----------------------------------------------------------------------------
// 描述: 检查异步连接的状态
// 参数:
//   timeoutMSecs - 最多等待的毫秒数，为-1表示不等待
// 返回:
//   ACS_CONNECTING - 尚未连接完毕，且尚未发生错误
//   ACS_CONNECTED  - 连接已建立成功
//   ACS_FAILED     - 连接过程中发生了错误，导致连接失败
// 备注:
//   不抛异常。
//-----------------------------------------------------------------------------
int BaseTcpClient::checkAsyncConnectState(int timeoutMSecs)
{
    if ((connection_ == nullptr) || !getSocket().isActive())
        return static_cast<int>(ASYNC_CONNECT_STATE::ACS_FAILED);

    const int WAIT_STEP = 100;   // ms
    ASYNC_CONNECT_STATE result = ASYNC_CONNECT_STATE::ACS_CONNECTING;
    SOCKET handle = getSocket().getHandle();
    fd_set rset, wset;
    struct timeval tv;
    int ms = 0;

    timeoutMSecs = max(timeoutMSecs, -1);

    while (true)
    {
        tv.tv_sec = 0;
        tv.tv_usec = (timeoutMSecs? WAIT_STEP * 1000 : 0);
        FD_ZERO(&rset);
        FD_SET((DWORD)handle, &rset);
        wset = rset;

        int r = select(handle + 1, &rset, &wset, nullptr, &tv);
        if (r > 0 && (FD_ISSET(handle, &rset) || FD_ISSET(handle, &wset)))
        {
            socklen_t errLen = sizeof(int);
            int errorCode = 0;
            // If error occurs
            if (getsockopt(handle, SOL_SOCKET, SO_ERROR, (char*)&errorCode, &errLen) < 0 || errorCode)
                result = ASYNC_CONNECT_STATE::ACS_FAILED;
            else
                result = ASYNC_CONNECT_STATE::ACS_CONNECTED;
        }
        else if (r < 0)
        {
            if (iseSocketGetLastError() != SS_EINTR)
                result = ASYNC_CONNECT_STATE::ACS_FAILED;
        }

        if (result != ASYNC_CONNECT_STATE::ACS_CONNECTING)
            break;

        // Check timeout
        if (timeoutMSecs != -1)
        {
            ms += WAIT_STEP;
            if (ms >= timeoutMSecs)
                break;
        }
    }

    return static_cast<int>(result);
}

//-----------------------------------------------------------------------------

void BaseTcpClient::disconnect()
{
    if (connection_)
    {
        connection_->disconnect();
        connection_.reset();
    }
}

//-----------------------------------------------------------------------------

BaseTcpConnection& BaseTcpClient::getConnection()
{
    ensureConnCreated();
    return *connection_;
}

//-----------------------------------------------------------------------------

void BaseTcpClient::ensureConnCreated()
{
    if (!connection_)
        connection_.reset(createConnection());
}

//-----------------------------------------------------------------------------

TcpSocket& BaseTcpClient::getSocket()
{
    ISE_ASSERT(connection_);
    return connection_->getSocket();
}

///////////////////////////////////////////////////////////////////////////////
// class BaseTcpServer

BaseTcpServer::BaseTcpServer() :
    localPort_(0)
{
    // nothing
}

//-----------------------------------------------------------------------------

BaseTcpServer::~BaseTcpServer()
{
    close();
}

//-----------------------------------------------------------------------------
// 描述: 开启TCP服务器
//-----------------------------------------------------------------------------
void BaseTcpServer::open()
{
    try
    {
        if (!isActive())
        {
            socket_.open();
            socket_.bind(localPort_);
            if (listen(socket_.getHandle(), LISTEN_QUEUE_SIZE) < 0)
                iseThrowSocketLastError();
            startListenerThread();
        }
    }
    catch (SocketException&)
    {
        close();
        throw;
    }
}

//-----------------------------------------------------------------------------
// 描述: 关闭TCP服务器
//-----------------------------------------------------------------------------
void BaseTcpServer::close()
{
    if (isActive())
    {
        stopListenerThread();
        socket_.close();
    }
}

//-----------------------------------------------------------------------------
// 描述: 开启/关闭TCP服务器
//-----------------------------------------------------------------------------
void BaseTcpServer::setActive(bool value)
{
    if (isActive() != value)
    {
        if (value) open();
        else close();
    }
}

//-----------------------------------------------------------------------------
// 描述: 设置TCP服务器监听端口
//-----------------------------------------------------------------------------
void BaseTcpServer::setLocalPort(WORD value)
{
    if (value != localPort_)
    {
        if (isActive()) close();
        localPort_ = value;
    }
}

//-----------------------------------------------------------------------------
// 描述: 设置“创建新连接”的回调
//-----------------------------------------------------------------------------
void BaseTcpServer::setCreateConnCallback(const TcpSvrCreateConnCallback& callback)
{
    onCreateConn_ = callback;
}

//-----------------------------------------------------------------------------
// 描述: 设置“收到连接”的回调
//-----------------------------------------------------------------------------
void BaseTcpServer::setAcceptConnCallback(const TcpSvrAcceptConnCallback& callback)
{
    onAcceptConn_ = callback;
}

//-----------------------------------------------------------------------------
// 描述: 启动监听线程
//-----------------------------------------------------------------------------
void BaseTcpServer::startListenerThread()
{
    if (!listenerThread_)
    {
        listenerThread_.reset(new TcpListenerThread(this));
        listenerThread_->run();
    }
}

//-----------------------------------------------------------------------------
// 描述: 停止监听线程
//-----------------------------------------------------------------------------
void BaseTcpServer::stopListenerThread()
{
    if (listenerThread_)
    {
        listenerThread_->terminate();
        listenerThread_->waitFor();
        listenerThread_.reset();
    }
}

//-----------------------------------------------------------------------------
// 描述: 创建连接对象
//-----------------------------------------------------------------------------
BaseTcpConnection* BaseTcpServer::createConnection(SOCKET socketHandle)
{
    BaseTcpConnection *result = nullptr;

    if (onCreateConn_)
        onCreateConn_(this, socketHandle, result);

    if (result == nullptr)
        return new BaseTcpConnection(socketHandle);

    return result;
}

//-----------------------------------------------------------------------------
// 描述: 收到连接 (注: 本函数接管 connection 的所有权)
//-----------------------------------------------------------------------------
void BaseTcpServer::acceptConnection(BaseTcpConnection *connection)
{
    // 所有权在此处交接: 先装进句柄，再决定交给谁。
    TcpConnectionHandle handle(connection);

    // 无人接手时，句柄出作用域即销毁连接对象。
    if (onAcceptConn_)
        onAcceptConn_(this, handle.release());
}

///////////////////////////////////////////////////////////////////////////////
// class UdpListenerThread

UdpListenerThread::UdpListenerThread(UdpListenerThreadPool *threadPool, int /*index*/) :
    threadPool_(threadPool)
{
    setAutoDelete(true);
    udpServer_ = &(threadPool->getUdpServer());
    threadPool_->registerThread(this);
}

//-----------------------------------------------------------------------------

UdpListenerThread::~UdpListenerThread()
{
    threadPool_->unregisterThread(this);
}

//-----------------------------------------------------------------------------
// 描述: UDP服务器监听工作
//-----------------------------------------------------------------------------
void UdpListenerThread::execute()
{
    const int MAX_UDP_BUFFER_SIZE = 8192;   // UDP数据包最大字节数
    const int SELECT_WAIT_MSEC    = 100;    // 每次等待时间 (毫秒)

    fd_set fds;
    struct timeval tv;
    SOCKET socketHandle = udpServer_->getHandle();
    Buffer packetBuffer(MAX_UDP_BUFFER_SIZE);
    InetAddress peerAddr;
    int r, n;

    while (!isTerminated() && udpServer_->isActive())
    try
    {
        // 设定每次等待时间
        tv.tv_sec = 0;
        tv.tv_usec = SELECT_WAIT_MSEC * 1000;

        FD_ZERO(&fds);
        FD_SET((UINT)socketHandle, &fds);

        r = select(socketHandle + 1, &fds, nullptr, nullptr, &tv);

        if (r > 0 && udpServer_->isActive() && FD_ISSET(socketHandle, &fds))
        {
            // 字节从这里进入本工程: 一次 recvfrom 之后, 收下多少就是这段
            // span 有多长, 之后再没有人重新报一遍长度。
            std::span<std::byte> room =
                asWritableBytes(packetBuffer.data(), MAX_UDP_BUFFER_SIZE);
            n = udpServer_->recvBuffer(room, peerAddr);
            if (n > 0)
            {
                udpServer_->dataReceived(room.first(static_cast<size_t>(n)), peerAddr);
            }
        }
        else if (r < 0)
        {
            int errorCode = iseSocketGetLastError();
            if (errorCode != SS_EINTR && errorCode != SS_EINPROGRESS)
                break;  // error
        }
    }
    catch (Exception&)
    {}
}

///////////////////////////////////////////////////////////////////////////////
// class UdpListenerThreadPool

UdpListenerThreadPool::UdpListenerThreadPool(BaseUdpServer *udpServer) :
    udpServer_(udpServer),
    maxThreadCount_(0)
{
    // nothing
}

//-----------------------------------------------------------------------------

UdpListenerThreadPool::~UdpListenerThreadPool()
{
    // nothing
}

//-----------------------------------------------------------------------------
// 描述: 注册线程
//-----------------------------------------------------------------------------
void UdpListenerThreadPool::registerThread(UdpListenerThread *thread)
{
    threadList_.add(thread);
}

//-----------------------------------------------------------------------------
// 描述: 注销线程
//-----------------------------------------------------------------------------
void UdpListenerThreadPool::unregisterThread(UdpListenerThread *thread)
{
    threadList_.remove(thread);
}

//-----------------------------------------------------------------------------
// 描述: 创建并启动线程
//-----------------------------------------------------------------------------
void UdpListenerThreadPool::startThreads()
{
    for (int i = 0; i < maxThreadCount_; i++)
    {
        // 线程自行销毁(见 UdpListenerThread 构造函数中的 setAutoDelete)，
        // run() 成功后所有权交给它自己。
        std::unique_ptr<UdpListenerThread> thread(new UdpListenerThread(this, i));
        thread->run();
        thread.release();
    }
}

//-----------------------------------------------------------------------------
// 描述: 通知并等待所有线程退出
//-----------------------------------------------------------------------------
void UdpListenerThreadPool::stopThreads()
{
    const int MAX_WAIT_FOR_SECS = 5;
    threadList_.waitForAllThreads(MAX_WAIT_FOR_SECS);
}

///////////////////////////////////////////////////////////////////////////////
// class TcpListenerThread

TcpListenerThread::TcpListenerThread(BaseTcpServer *tcpServer) :
    tcpServer_(tcpServer)
{
    setAutoDelete(false);
}

//-----------------------------------------------------------------------------
// 描述: TCP服务器监听工作
//-----------------------------------------------------------------------------
void TcpListenerThread::execute()
{
    const int SELECT_WAIT_MSEC = 100;    // 每次等待时间 (毫秒)

    fd_set fds;
    struct timeval tv;
    SockAddr Addr;
    socklen_t nSockLen = sizeof(Addr);
    SOCKET socketHandle = tcpServer_->getSocket().getHandle();
    InetAddress peerAddr;
    SOCKET acceptHandle;
    int r;

    while (!isTerminated() && tcpServer_->isActive())
    try
    {
        // 设定每次等待时间
        tv.tv_sec = 0;
        tv.tv_usec = SELECT_WAIT_MSEC * 1000;

        FD_ZERO(&fds);
        FD_SET((UINT)socketHandle, &fds);

        r = select(socketHandle + 1, &fds, nullptr, nullptr, &tv);

        if (r > 0 && tcpServer_->isActive() && FD_ISSET(socketHandle, &fds))
        {
            acceptHandle = accept(socketHandle, (struct sockaddr*)&Addr, &nSockLen);
            if (acceptHandle != INVALID_SOCKET)
            {
                peerAddr = InetAddress(ntohl(Addr.sin_addr.s_addr), ntohs(Addr.sin_port));
                tcpServer_->acceptConnection(
                    tcpServer_->createConnection(acceptHandle));
            }
        }
        else if (r < 0)
        {
            int errorCode = iseSocketGetLastError();
            if (errorCode != SS_EINTR && errorCode != SS_EINPROGRESS)
                break;  // error
        }
    }
    catch (Exception&)
    {}
}

///////////////////////////////////////////////////////////////////////////////

} // namespace ise
