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
// 文件名称: ise_global_defs.h
// 功能描述: 全局定义
///////////////////////////////////////////////////////////////////////////////

#ifndef _ISE_GLOBAL_DEFS_H_
#define _ISE_GLOBAL_DEFS_H_

#include "ise/main/ise_options.h"

#include <stdio.h>
#include <cstdlib>

#ifdef ISE_COMPILER_VC
#include <basetsd.h>
#else
#include <stdint.h>
#endif

#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

// boost headers
// function.hpp/bind.hpp: 只有 ISE_WINDOWS 分支还需要它们（ise_iocp.h 的 IocpCallback），
// 本环境编译的代码已改用 <functional>，所以它们跟着使用它们的代码进分支。
#ifdef ISE_WINDOWS
#include "boost/function.hpp"
#include "boost/bind.hpp"
#endif
#include "boost/utility.hpp"

#include "ise/main/ise_stldefs.h"

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// 类型定义

#ifdef ISE_COMPILER_VC
typedef INT8      int8_t;
typedef INT16     int16_t;
typedef INT32     int32_t;
typedef INT64     int64_t;
typedef UINT8     uint8_t;
typedef UINT16    uint16_t;
typedef UINT32    uint32_t;
typedef UINT64    uint64_t;
#endif

#ifdef ISE_LINUX
typedef int8_t    INT8;
typedef int16_t   INT16;
typedef int32_t   INT32;
typedef int64_t   INT64;

typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;

typedef uint8_t   BYTE;
typedef uint16_t  WORD;
typedef uint32_t  DWORD;
typedef uint32_t  UINT;

typedef int       HANDLE;
typedef void*     PVOID;
typedef UINT8*    PBYTE;
typedef UINT16*   PWORD;
typedef UINT32*   PDWORD;
#endif

#ifdef ISE_WINDOWS
typedef UINT32 THREAD_ID;
#endif
#ifdef ISE_LINUX
typedef pid_t THREAD_ID;
#endif

typedef void* POINTER;

typedef std::vector<string> StringArray;
typedef std::vector<int> IntegerArray;
typedef std::vector<bool> BooleanArray;
typedef std::set<int> IntegerSet;

typedef std::any Context;
typedef std::function<void(void)> Functor;

typedef std::function<void(void)> TimerCallback;
typedef INT64 TimerId;

///////////////////////////////////////////////////////////////////////////////
// 常量定义

#ifdef ISE_WINDOWS
const char PATH_DELIM               = '\\';
const char DRIVER_DELIM             = ':';
const char FILE_EXT_DELIM           = '.';
const char* const S_CRLF            = "\r\n";
#endif
#ifdef ISE_LINUX
inline const char PATH_DELIM               = '/';
inline const char FILE_EXT_DELIM           = '.';
inline const char* const S_CRLF            = "\n";
#endif

// 文件属性
inline const unsigned int FA_READ_ONLY     = 0x00000001;
inline const unsigned int FA_HIDDEN        = 0x00000002;
inline const unsigned int FA_SYS_FILE      = 0x00000004;
inline const unsigned int FA_VOLUME_ID     = 0x00000008;    // Windows Only
inline const unsigned int FA_DIRECTORY     = 0x00000010;
inline const unsigned int FA_ARCHIVE       = 0x00000020;
inline const unsigned int FA_SYM_LINK      = 0x00000040;    // Linux Only
inline const unsigned int FA_ANY_FILE      = 0x0000007F;

// 时间相关
inline const int MILLISECS_PER_SECOND   = 1000;
inline const int SECONDS_PER_MINUTE     = 60;
inline const int MINUTES_PER_HOUR       = 60;
inline const int HOURS_PER_DAY          = 24;
inline const int SECONDS_PER_HOUR       = 3600;
inline const int SECONDS_PER_DAY        = 86400;

// 范围值
#define MAXCHAR     0x7f
#define MAXSHORT    0x7fff
#define MAXINT      0x7fffffff
#define MAXBYTE     0xff
#define MAXWORD     0xffff
#define MAXDWORD    0xffffffff

// Strings 相关常量定义
#define DEFAULT_DELIMITER            ','
#define DEFAULT_QUOTE_CHAR           '\"'
#define DEFAULT_NAME_VALUE_SEP       '='
#define DEFAULT_LINE_BREAK           S_CRLF

#ifdef ISE_LINUX
#define INVALID_HANDLE_VALUE        ((HANDLE)-1)
#endif

#define TRUE_STR                    "true"
#define FALSE_STR                   "false"

inline const int TIMEOUT_INFINITE = -1;

inline const Context EMPTY_CONTEXT = Context();

///////////////////////////////////////////////////////////////////////////////
// 一段字节
//
// 收发数据的那条路上, 一个缓冲区从前是两个参数: 一个 void* 和一个 int。两个
// 参数可以各走各的 —— 传错长度、少传一个、在中途被人改掉其中一个, 都编译得
// 过。std::span 把它们绑成一个值, 于是"这段内存有多长"不再是调用者每次重新
// 声明一遍的事。
//
// 下面这两个函数只在一个地方用: 字节从 C 的世界进来的那一刻 (一次 recvfrom、
// 一个 Packet 自己的缓冲区)。进来之后就一直是 span, 直到最后一寸交给系统调用
// 为止。
inline std::span<const std::byte> asBytes(const void *data, int size)
{
    return std::span<const std::byte>(
        static_cast<const std::byte*>(data), size > 0 ? static_cast<size_t>(size) : 0);
}

inline std::span<std::byte> asWritableBytes(void *data, int size)
{
    return std::span<std::byte>(
        static_cast<std::byte*>(data), size > 0 ? static_cast<size_t>(size) : 0);
}

///////////////////////////////////////////////////////////////////////////////
// 宏定义

// 本工程内已无人使用它，但它是公开头文件里的声明，仍按原样保留。
#define SAFE_DELETE(x)          { delete x; x = NULL; }
#define CATCH_ALL_EXCEPTION(x)  try { x; } catch(...) {}

///////////////////////////////////////////////////////////////////////////////

} // namespace ise

#endif // _ISE_GLOBAL_H_
