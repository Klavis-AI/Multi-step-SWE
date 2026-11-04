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
// 文件名称: ise_assert.cpp
// 功能描述: 断言支持
///////////////////////////////////////////////////////////////////////////////

#include "ise/main/ise_assert.h"
// 配置层: 宏不跨模块, 这一层永远是文本包含。
#include "ise/main/ise_options.h"
#include "ise/main/ise_global_defs.h"
#include "ise/main/ise_stldefs.h"
#include "ise/main/ise_assert.h"

// 这些平台声明以前是随项目头文件一起进来的。
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/timeb.h>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cstdarg>
#include <stdint.h>
#include <semaphore.h>
#include <signal.h>
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
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>
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

import ise.exceptions;
import ise.classes;
import ise.sysutils;

namespace ise
{

///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// 描述: 内部使用的断言函数。输出日志，并抛出异常。
//-----------------------------------------------------------------------------
void internalAssert(const char *condition, const char *fileName, int lineNumber)
{
    SimpleException e(std::format("Assertion failed: {}", condition).c_str(), fileName, lineNumber);
    logger().writeException(e);
    throw e;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace ise
