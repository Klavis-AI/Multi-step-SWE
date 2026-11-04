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
// 文件名称: ise_exceptions.cpp
// 功能描述: 异常类
///////////////////////////////////////////////////////////////////////////////

// 配置层: 宏不跨模块, 这一层永远是文本包含。
#include "ise/main/ise_options.h"
#include "ise/main/ise_global_defs.h"
#include "ise/main/ise_stldefs.h"
#include "ise/main/ise_assert.h"

// 这些平台声明以前是随项目头文件一起进来的。
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
#include <cstdlib>
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
#include <stdint.h>
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <concepts>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <deque>
#include <algorithm>

import ise.exceptions;
import ise.sysutils;

namespace ise
{

///////////////////////////////////////////////////////////////////////////////

const char* const EXCEPTION_LOG_PREFIX = "ERROR: ";

///////////////////////////////////////////////////////////////////////////////
// class Exception

//-----------------------------------------------------------------------------
// 描述: 用于 std::exception 返回错误信息
//-----------------------------------------------------------------------------
const char* Exception::what() const noexcept
{
    what_ = getErrorMessage();
    return what_.c_str();
}

//-----------------------------------------------------------------------------
// 描述: 返回用于 Log 的字符串
//-----------------------------------------------------------------------------
string Exception::makeLogStr() const
{
    return string(EXCEPTION_LOG_PREFIX) + getErrorMessage();
}

///////////////////////////////////////////////////////////////////////////////
// class SimpleException

SimpleException::SimpleException(const char *errorMsg,
    const char *srcFileName, int srcLineNumber)
{
    if (errorMsg)
        errorMsg_ = errorMsg;
    if (srcFileName)
        srcFileName_ = srcFileName;
    srcLineNumber_ = srcLineNumber;
}

//-----------------------------------------------------------------------------

string SimpleException::makeLogStr() const
{
    string result(getErrorMessage());

    if (!srcFileName_.empty() && srcLineNumber_ >= 0)
        result = result + " (" + srcFileName_ + ":" + intToStr(srcLineNumber_) + ")";

    result = string(EXCEPTION_LOG_PREFIX) + result;

    return result;
}

///////////////////////////////////////////////////////////////////////////////
// class FileException

FileException::FileException(const char *fileName, int errorCode, const char *errorMsg) :
    fileName_(fileName),
    errorCode_(errorCode)
{
    if (errorMsg == nullptr)
        errorMsg_ = sysErrorMessage(errorCode);
    else
        errorMsg_ = errorMsg;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace ise
