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
// ise.exceptions.cppm -- the interface unit for module `ise.exceptions`.
//
// 全局模块片段: 标准库头文件与配置层头文件。配置层是宏, 宏不能跨模块,
// 所以 ise_options.h 这一层永远是头文件。
///////////////////////////////////////////////////////////////////////////////

module;

#include "ise/main/ise_options.h"
#include "ise/main/ise_global_defs.h"
#include "ise/main/ise_stldefs.h"
#include "ise/main/ise_assert.h"

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
#include <string>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <deque>
#include <algorithm>

#include "ise/main/ise_err_msgs.h"

export module ise.exceptions;

export import ise.errmsgs;

// `extern "C++"` 把这些声明留在全局模块里, 而不是把它们绑到本模块上。
// 驱动程序和 examples 仍然用 #include 文本包含同一批头文件, 两边必须是
// 同一批实体, 否则链接不到一起。
export extern "C++" {

#include "ise/main/ise_exceptions.h"

}
