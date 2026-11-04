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
// ise_sys_utils.h
///////////////////////////////////////////////////////////////////////////////

#ifndef _ISE_SYS_UTILS_H_
#define _ISE_SYS_UTILS_H_

#include "ise/main/ise_options.h"
#include "ise/main/ise_concepts.h"

#ifdef ISE_WINDOWS
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <string>
#include <windows.h>
#endif

#ifdef ISE_LINUX
#include <stdio.h>
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <iostream>
#include <string>
#endif

#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "ise/main/ise_global_defs.h"

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// 提前声明

class Buffer;
class StrList;

///////////////////////////////////////////////////////////////////////////////
// 类型定义

// 文件查找记录
struct FileFindItem
{
    INT64 fileSize;         // 文件大小
    string fileName;   // 文件名(不含路径)
    UINT attr;              // 文件属性
};

typedef std::vector<FileFindItem> FileFindResult;

///////////////////////////////////////////////////////////////////////////////
// 杂项函数

//-----------------------------------------------------------------------------
//-- 字符串函数:

[[nodiscard]] bool isIntStr(const string& str);
[[nodiscard]] bool isInt64Str(const string& str);
[[nodiscard]] bool isFloatStr(const string& str);
[[nodiscard]] bool isBoolStr(const string& str);

[[nodiscard]] int strToInt(const string& str, int defaultVal = 0);
[[nodiscard]] INT64 strToInt64(const string& str, INT64 defaultVal = 0);

// 整型转换成字符串。取代了原先 int 与 INT64 两个重载: 用哪一种宽度
// 打印, 是一个只跟类型有关的问题, 因此在编译期回答。
template <std::integral T>
[[nodiscard]] string intToStr(T value)
{
    if constexpr (sizeof(T) > sizeof(int))
        return std::to_string(static_cast<long long>(value));
    else
        return std::to_string(static_cast<int>(value));
}

[[nodiscard]] double strToFloat(const string& str, double defaultVal = 0);
[[nodiscard]] string floatToStr(double value, const char *format = "%f");
[[nodiscard]] bool strToBool(const string& str, bool defaultVal = false);
[[nodiscard]] string boolToStr(bool value, bool useBoolStrs = false);

void formatStringV(string& result, const char *format, va_list argList);

namespace detail {

// formatString 的唯一出口: 把已经检查过类型的实参交给 C 的可变参数约定。
// 这是本工程里仅存的一个 `...` 函数, 不属于公开接口。
string formatStringRaw(const char *format, ...);

} // namespace detail

// 返回格式化后的字符串。实参的类型写在签名上: 一个不满足 VarargSafe 的实参
// —— 比如直接传进来的 std::string —— 使这个模板根本不是候选, 编译器指的是
// 调用处, 而不是模板体里某一行。
template <VarargSafe... Args>
[[nodiscard]] string formatString(const char *format, Args... args)
{
    return detail::formatStringRaw(format, args...);
}

[[nodiscard]] bool sameText(const string& str1, const string& str2);
[[nodiscard]] int compareText(const char* str1, const char* str2);
[[nodiscard]] string trimString(std::string_view str);
[[nodiscard]] string upperCase(std::string_view str);
[[nodiscard]] string lowerCase(std::string_view str);
[[nodiscard]] string repalceString(std::string_view sourceStr, std::string_view oldPattern,
    std::string_view newPattern, bool replaceAll = false, bool caseSensitive = true);
void splitString(std::string_view sourceStr, char splitter, StrList& strList,
    bool trimResult = false);
void splitStringToInt(std::string_view sourceStr, char splitter, IntegerArray& intList);
string fetchStr(string& inputStr, char delimiter = ' ', bool del = true);
[[nodiscard]] string addThousandSep(const INT64& number);

[[nodiscard]] string getQuotedStr(const char* str, char quoteChar = '"');
string extractQuotedStr(const char*& str, char quoteChar = '"');
[[nodiscard]] string getDequotedStr(const char* str, char quoteChar = '"');

//-----------------------------------------------------------------------------
//-- 文件和目录:

[[nodiscard]] bool fileExists(const string& fileName);
[[nodiscard]] bool directoryExists(const string& dir);
bool createDir(const string& dir);
bool deleteDir(const string& dir, bool recursive = false);
[[nodiscard]] string extractFilePath(std::string_view fileName);
[[nodiscard]] string extractFileName(std::string_view fileName);
[[nodiscard]] string extractFileExt(std::string_view fileName);
[[nodiscard]] string changeFileExt(std::string_view fileName, std::string_view ext);
bool forceDirectories(string dir);
bool deleteFile(const string& fileName);
bool removeFile(const string& fileName);
bool renameFile(const string& oldFileName, const string& newFileName);
// 文件大小；路径不存在、不可达或不是普通文件时没有值。
// getFileSize() 保留了它的 -1 约定，这个函数没有。
[[nodiscard]] std::optional<INT64> tryGetFileSize(const string& fileName);
[[nodiscard]] INT64 getFileSize(const string& fileName);
void findFiles(const string& path, UINT attr, FileFindResult& findResult);
[[nodiscard]] string pathWithSlash(std::string_view path);
[[nodiscard]] string pathWithoutSlash(std::string_view path);
[[nodiscard]] string getAppExeName(bool includePath = true);
[[nodiscard]] string getAppPath();
[[nodiscard]] string getAppSubPath(const string& subDir = "");

//-----------------------------------------------------------------------------
//-- 系统相关:

[[nodiscard]] int getLastSysError();
[[nodiscard]] THREAD_ID getCurThreadId();
[[nodiscard]] string sysErrorMessage(int errorCode);
void sleepSeconds(double seconds, bool allowInterrupt = true);
[[nodiscard]] UINT64 getCurTicks();
[[nodiscard]] UINT64 getTickDiff(UINT64 oldTicks, UINT64 newTicks);

//-----------------------------------------------------------------------------
//-- 其它函数:

void randomize();
int getRandom(int min, int max);
void generateRandomList(int startNumber, int endNumber, int *randomList);

// 这四个模板与上面的自由函数是同一类东西: 算出一个答案, 别的什么也不做。
// swap 不在其列 -- 它换的是调用者的两个对象, 返回值本来就没有。
template <std::totally_ordered T>
[[nodiscard]] const T& min(const T& a, const T& b) { return ((a < b)? a : b); }

template <std::totally_ordered T>
[[nodiscard]] const T& max(const T& a, const T& b) { return ((a < b)? b : a); }

template <std::totally_ordered T>
[[nodiscard]] const T& ensureRange(const T& value, const T& minVal, const T& maxVal)
    { return (value > maxVal) ? maxVal : (value < minVal ? minVal : value); }

template <typename T>
    requires std::copy_constructible<T> && std::assignable_from<T&, const T&>
void swap(T& a, T& b) { T temp(a); a=b; b=temp; }

template <std::totally_ordered T>
[[nodiscard]] int compare(const T& a, const T& b) { return (a < b) ? -1 : (a > b ? 1 : 0); }

///////////////////////////////////////////////////////////////////////////////

} // namespace ise

#endif // _ISE_SYS_UTILS_H_
