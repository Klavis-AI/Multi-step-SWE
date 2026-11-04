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
// ise_coroutine.h
///////////////////////////////////////////////////////////////////////////////

#ifndef _ISE_COROUTINE_H_
#define _ISE_COROUTINE_H_

#include "ise/main/ise_options.h"
#include "ise/main/ise_classes.h"

#include <concepts>
#include <coroutine>
#include <exception>
#include <iterator>
#include <span>
#include <utility>

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// class ConnectionTask - 一条连接的会话协程
//
// 说明:
// 1. 这是一个 detached task: 框架在连接建立时调用一次业务的会话函数，拿到的
//    ConnectionTask 立刻丢弃，协程自己持有自己的帧，跑完最后一行时自己销毁。
//    调用方无须、也无法等待它。
// 2. 会话函数按值收下 TcpConnectionPtr，因此一个挂起着的会话协程自己就让连接
//    活着。反过来连接不持有协程帧，两边没有环。
// 3. 异常不能穿过事件循环的栈: 事件循环上再没有一个业务的调用者可以接住它。
//    逃出会话协程的异常在这里被记下来然后咽掉，和从前逃出 onTcpRecvComplete()
//    的异常一样，只是从前那是框架替业务咽的。

class ConnectionTask
{
public:
    struct promise_type
    {
        ConnectionTask get_return_object() const noexcept { return ConnectionTask(); }
        // 立即开始: 会话函数被调用时就跑到它的第一个挂起点，正如从前
        // onTcpConnected() 被调用时就跑完它的函数体。
        std::suspend_never initial_suspend() const noexcept { return {}; }
        // 不再挂起: 协程跑完最后一行就销毁自己的帧。没有人在等它。
        std::suspend_never final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}

        void unhandled_exception() const noexcept
        {
            try
            {
                std::rethrow_exception(std::current_exception());
            }
            catch (std::exception& e)
            {
                logger().writeStr(std::string("connection coroutine: ") + e.what());
            }
            catch (...)
            {
                logger().writeStr("connection coroutine: unknown exception");
            }
        }
    };
};

///////////////////////////////////////////////////////////////////////////////
// struct CoroutineCompletion - 一次收发完成时该唤醒谁
//
// 说明:
// 1. 每一个发送/接收任务都带着一个 Context (std::any)。从前那里放的是业务自己
//    的标记，业务在 onTcpSendComplete() 里再把它认出来 —— 因为函数已经返回过
//    一次，除了这个标记它没有别的办法记住自己刚才在做什么。协程不必记: 它没有
//    返回过，局部变量都还在。于是 Context 里现在放的是"接着往下跑"本身。
// 2. completed 和 packet 指向挂起着的协程帧里的局部变量。帧在挂起期间不动，
//    所以这两个指针在挂起期间一直有效；协程一旦恢复，这个 Context 就再没有人
//    看了。

struct CoroutineCompletion
{
    std::coroutine_handle<> handle;             // 挂起在这个任务上的协程
    bool *completed;                            // 任务是否真的完成 (而非连接断开)
    std::span<const std::byte> *packet;         // 收到的那一段; 发送任务为 nullptr

    CoroutineCompletion() noexcept :
        handle(), completed(nullptr), packet(nullptr) {}

    CoroutineCompletion(std::coroutine_handle<> h, bool *done,
        std::span<const std::byte> *received) noexcept :
        handle(h), completed(done), packet(received) {}
};

///////////////////////////////////////////////////////////////////////////////
// template class Generator<T> - 惰性序列
//
// 说明:
// 1. co_yield 一个值就停下来，下一次取值时才接着算下一个。序列本身不必先被整个
//    物化成一个容器，也不必事先知道自己有多长 —— 一个无穷序列在这里是合法的。
// 2. 只支持单遍遍历: begin() 把协程推进到第一个值，operator++ 推进到下一个，
//    done() 之后就是 end()。这正是 input range 的形状。
// 3. 约束写在这里而不是留给实例化时的报错: promise 里存着一个 T, co_yield 的
//    那个值是移动进去的, 所以 T 要默认可构造、可移动 —— 这正是第 7 步给
//    这个项目的每一个类模板定下的规矩, 一个新写的模板不该是例外。

template <typename T>
    requires std::default_initializable<T> && std::movable<T>
class Generator
{
public:
    struct promise_type
    {
        T value_;
        std::exception_ptr error_;

        Generator get_return_object() noexcept
        { return Generator(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        std::suspend_always yield_value(T value)
        { value_ = std::move(value); return {}; }
        void return_void() const noexcept {}
        void unhandled_exception() noexcept { error_ = std::current_exception(); }
    };

    class Iterator
    {
    public:
        typedef std::input_iterator_tag iterator_category;
        typedef T value_type;
        typedef std::ptrdiff_t difference_type;
        typedef const T* pointer;
        typedef const T& reference;

        explicit Iterator(std::coroutine_handle<promise_type> handle) noexcept :
            handle_(handle) {}

        Iterator& operator++()
        {
            handle_.resume();
            rethrowIfFailed();
            return *this;
        }
        void operator++(int) { ++*this; }

        const T& operator*() const noexcept { return handle_.promise().value_; }
        const T* operator->() const noexcept { return &handle_.promise().value_; }

        bool operator==(std::default_sentinel_t) const noexcept
        { return !handle_ || handle_.done(); }

    private:
        void rethrowIfFailed() const
        {
            if (handle_.promise().error_)
                std::rethrow_exception(handle_.promise().error_);
        }

    private:
        std::coroutine_handle<promise_type> handle_;
    };

public:
    Generator() noexcept : handle_() {}
    explicit Generator(std::coroutine_handle<promise_type> handle) noexcept :
        handle_(handle) {}
    Generator(Generator&& rhs) noexcept :
        handle_(std::exchange(rhs.handle_, {})) {}
    Generator& operator=(Generator&& rhs) noexcept
    {
        if (this != &rhs)
        {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(rhs.handle_, {});
        }
        return *this;
    }
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;
    ~Generator() { if (handle_) handle_.destroy(); }

    Iterator begin()
    {
        if (handle_) handle_.resume();
        Iterator iter(handle_);
        if (handle_ && handle_.promise().error_)
            std::rethrow_exception(handle_.promise().error_);
        return iter;
    }
    std::default_sentinel_t end() const noexcept { return {}; }

private:
    std::coroutine_handle<promise_type> handle_;
};

///////////////////////////////////////////////////////////////////////////////

} // namespace ise

#endif // _ISE_COROUTINE_H_
