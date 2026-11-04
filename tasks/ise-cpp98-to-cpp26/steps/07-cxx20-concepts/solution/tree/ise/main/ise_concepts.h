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
// ise_concepts.h
///////////////////////////////////////////////////////////////////////////////

#ifndef _ISE_CONCEPTS_H_
#define _ISE_CONCEPTS_H_

#include "ise/main/ise_options.h"
#include "ise/main/ise_global_defs.h"

#include <concepts>
#include <type_traits>

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// 本工程自己需要、而标准库没有的四个 concept。
//
// 标准库已经说清楚的事情不在这里重复: 一个整数是 std::integral, 一个有符号
// 整数是 std::signed_integral, 一个能互相比较大小的类型是
// std::totally_ordered, 一个能拷贝构造的类型是 std::copy_constructible。
// 给它们起一个本工程的别名, 读者就得多记一层, 而多出来的那一层什么也没说。
//
// 下面这四个不一样: 它们是这些模板对 T 提出的、标准库里没有现成说法的要求。
// 从前它们写在注释里, 上一个里程碑把它们写成了 static_assert —— 那已经是让
// 编译器读得懂, 但代价是 T 不合适时模板照样实例化, 错误出在模板体里。写成
// concept 之后, 不满足的 T 根本不是一个成立的候选, 编译器指的是调用处。
///////////////////////////////////////////////////////////////////////////////

// CallbackList 在 registerCallback() 里丢弃"空"的回调, 所以 T 必须能回答
// 自己空不空。std::function 能; 一个 lambda 的闭包类型不能。
template <typename T>
concept EmptyTestable = requires (const T& callback)
{
    { static_cast<bool>(callback) } -> std::same_as<bool>;
};

// CustomObjectList 用 std::default_delete<T> 销毁它持有的对象。要求写成"能
// 这样销毁", 而不是 std::destructible: 后者对一个只声明过的类型也成立, 而
// 用 delete 删一个不完整类型是未定义行为, 正是这里要挡住的那一种。
template <typename T>
concept OwnedObject = requires (T *object)
{
    requires sizeof(T) > 0;
    delete object;
};

// Singleton 交出它用 new T() 造的那一个 T。
//
// 这个 concept 只能用在 instance() 上, 不能用在 Singleton<T> 的模板形参上:
// Singleton<T> 是作为 T 的基类被实例化的, 那时 T 还不完整, 而对一个不完整的
// 类型问"它是不是抽象类"是未定义的。instance() 是被调用时才实例化的, 那时 T
// 已经完整。上一个里程碑把 static_assert 写在 instance() 里, 原因相同。
template <typename T>
concept Instantiable = std::is_object_v<T> && !std::is_abstract_v<T>;

// C 的可变参数约定只搬运经过默认实参提升之后依然可用的类型。
template <typename T>
concept VarargSafe =
    std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>;

///////////////////////////////////////////////////////////////////////////////

} // namespace ise

#endif // _ISE_CONCEPTS_H_
