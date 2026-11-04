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
// ise.eventloop.cppm -- the interface unit for module `ise.eventloop`.
//
// 全局模块片段: 标准库头文件与配置层头文件。配置层是宏, 宏不能跨模块,
// 所以 ise_options.h 这一层永远是头文件。
///////////////////////////////////////////////////////////////////////////////

module;

#include "ise/main/ise_options.h"
#include "ise/main/ise_global_defs.h"
#include "ise/main/ise_stldefs.h"
#include "ise/main/ise_assert.h"

#include <sys/epoll.h>
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
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <cstdio>
#include <filesystem>
#include <expected>
#include <optional>
#include <string_view>
#include <pthread.h>
#include <system_error>
#include <thread>
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

#include "ise/main/ise_classes.h"
#include "ise/main/ise_socket.h"
#include "ise/main/ise_exceptions.h"
#include "ise/main/ise_sys_utils.h"
#include "ise/main/ise_thread.h"
#include "ise/main/ise_timer.h"
#include "ise/main/ise_err_msgs.h"
#include "ise/main/ise_concepts.h"

export module ise.eventloop;

export import ise.classes;
export import ise.sysutils;
export import ise.socket;
export import ise.exceptions;
export import ise.thread;
export import ise.timer;

// `extern "C++"` 把这些声明留在全局模块里, 而不是把它们绑到本模块上。
// 驱动程序和 examples 仍然用 #include 文本包含同一批头文件, 两边必须是
// 同一批实体, 否则链接不到一起。
export extern "C++" {

#include "ise/main/ise_epoll.h"
#include "ise/main/ise_iocp.h"
#include "ise/main/ise_event_loop.h"

}
