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
// ise_classes.h
///////////////////////////////////////////////////////////////////////////////

#ifndef _ISE_CLASSES_H_
#define _ISE_CLASSES_H_

#include "ise/main/ise_options.h"

#ifdef ISE_WINDOWS
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/timeb.h>
#include <iostream>
#include <fstream>
#include <string>
#include <windows.h>
#endif

#ifdef ISE_LINUX
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/timeb.h>
#include <sys/file.h>
#include <iostream>
#include <fstream>
#include <string>
#endif

// C++11 并发库: 互斥量、条件变量、原子量、一次性初始化。
// 这四个头文件不进平台分支 —— 它们在两个平台上都是同一个东西，正是换掉
// pthread_* 和 Interlocked* 的理由。
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <type_traits>

// C++20 库: 三路比较、格式化、连续内存的视图、范围算法。
#include <algorithm>
#include <compare>
#include <format>
#include <ranges>
#include <span>

#include "ise/main/ise_global_defs.h"
#include "ise/main/ise_err_msgs.h"
#include "ise/main/ise_exceptions.h"
#include "ise/main/ise_concepts.h"
#include "ise/main/ise_sys_utils.h"

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// class declares

class Buffer;
class DateTime;
class Timestamp;
class AutoFinalizer;
class AutoInvoker;
class AutoInvokable;
class Mutex;
class Semaphore;
class Condition;
class SignalMasker;
class AtomicInt;
class AtomicInt64;
class SeqNumberAlloc;
class Stream;
class MemoryStream;
class FileStream;
class PointerList;
class PropertyList;
class Strings;
class StrList;
class Url;
class Packet;
template<OwnedObject ObjectType> class CustomObjectList;
template<OwnedObject ObjectType> class ObjectList;
template<EmptyTestable T> class CallbackList;
template<std::copy_constructible T> class BlockingQueue;
class ObjectContext;
class Logger;

///////////////////////////////////////////////////////////////////////////////
// class Buffer - 缓存类

class Buffer
{
public:
    Buffer();
    Buffer(const Buffer& src);
    Buffer(Buffer&& src);
    explicit Buffer(int size);
    Buffer(const void *buffer, int size);
    virtual ~Buffer();

    Buffer& operator = (const Buffer& rhs);
    Buffer& operator = (Buffer&& rhs);
    const char& operator[] (int index) const { return (static_cast<char*>(buffer_))[index]; }
    char& operator[] (int index) { return const_cast<char&>((static_cast<const Buffer&>(*this))[index]); }
    operator char*() const { return static_cast<char*>(buffer_); }
    char* data() const { return static_cast<char*>(buffer_); }
    char* c_str() const;
    void assign(const void *buffer, int size);
    void clear() { setSize(0); }
    void setSize(int size, bool initZero = false);
    int getSize() const { return size_; }
    void ensureSize(int size) { if (getSize() < size) setSize(size); }
    void setPosition(int position);
    int getPosition() const { return position_; }

    bool loadFromStream(Stream& stream);
    bool loadFromFile(const string& fileName);
    bool saveToStream(Stream& stream);
    bool saveToFile(const string& fileName);
private:
    inline void init() { buffer_ = nullptr; size_ = 0; position_ = 0; }
    void assign(const Buffer& src);
    void verifyPosition();
protected:
    void *buffer_;
    int size_;
    int position_;
};

///////////////////////////////////////////////////////////////////////////////
// class DateTime - 日期时间类

class DateTime
{
public:
    DateTime() { time_ = 0; }
    DateTime(const DateTime& src) { time_ = src.time_; }
    DateTime(DateTime&& src) = default;
    explicit DateTime(time_t src) { time_ = src; }
    explicit DateTime(const string& src) { *this = src; }

    static DateTime now();

    DateTime& operator = (const DateTime& rhs) { time_ = rhs.time_; return *this; }
    DateTime& operator = (DateTime&& rhs) = default;
    DateTime& operator = (const time_t rhs) { time_ = rhs; return *this; }
    DateTime& operator = (const Timestamp& rhs);
    DateTime& operator = (const string& dateTimeStr);

    DateTime operator + (const DateTime& rhs) const { return DateTime(time_ + rhs.time_); }
    DateTime operator + (time_t rhs) const { return DateTime(time_ + rhs); }
    DateTime operator - (const DateTime& rhs) const { return DateTime(time_ - rhs.time_); }
    DateTime operator - (time_t rhs) const { return DateTime(time_ - rhs); }

    // 一个 time_t 的排序就是这个类的排序; 让编译器写出六个运算符。
    auto operator <=> (const DateTime& rhs) const = default;
    bool operator == (const DateTime& rhs) const = default;

    time_t epochTime() const { return time_; }

    void encodeDateTime(int year, int month, int day,
        int hour = 0, int minute = 0, int second = 0);
    void decodeDateTime(int *year, int *month, int *day,
        int *hour, int *minute, int *second,
        int *weekDay = nullptr, int *yearDay = nullptr) const;

    string toDateString(const string& dateSep = "-") const;
    string toDateTimeString(const string& dateSep = "-",
        const string& dateTimeSep = " ", const string& timeSep = ":") const;

private:
    time_t time_;     // (从1970-01-01 00:00:00 算起的秒数，UTC时间)
};

///////////////////////////////////////////////////////////////////////////////
// class Timestamp - 时间戳类 (毫秒精度)

class Timestamp
{
public:
    typedef INT64 TimeVal;   // UTC time value in millisecond resolution
    typedef INT64 TimeDiff;  // difference between two timestamps in milliseconds

public:
    Timestamp() { value_ = 0; }
    explicit Timestamp(TimeVal value) { value_ = value; }
    Timestamp(const Timestamp& src) { value_ = src.value_; }
    Timestamp(Timestamp&& src) = default;

    static Timestamp now();

    Timestamp& operator = (const Timestamp& rhs) { value_ = rhs.value_; return *this; }
    Timestamp& operator = (Timestamp&& rhs) = default;
    Timestamp& operator = (TimeVal rhs) { value_ = rhs; return *this; }
    Timestamp& operator = (const DateTime& rhs) { setEpochTime(rhs.epochTime()); return *this; }

    auto operator <=> (const Timestamp& rhs) const = default;
    bool operator == (const Timestamp& rhs) const = default;

    Timestamp  operator +  (TimeDiff d) const { return Timestamp(value_ + d); }
    Timestamp  operator -  (TimeDiff d) const { return Timestamp(value_ - d); }
    TimeDiff   operator -  (const Timestamp& rhs) const { return value_ - rhs.value_; }
    Timestamp& operator += (TimeDiff d) { value_ += d; return *this; }
    Timestamp& operator -= (TimeDiff d) { value_ -= d; return *this; }

    void update();
    void setEpochTime(time_t value);
    time_t epochTime() const;
    TimeVal epochMilliseconds() const;

    string toString(const string& dateSep = "-", const string& dateTimeSep = " ",
        const string& timeSep = ":", const string& microSecSep = ".") const;

private:
    TimeVal value_;
};

///////////////////////////////////////////////////////////////////////////////
// class AutoFinalizer - 基于 RAII 的自动析构器

class AutoFinalizer : boost::noncopyable
{
public:
    AutoFinalizer(const Functor& f) : f_(f) {}
    ~AutoFinalizer() { f_(); }
private:
    Functor f_;
};

///////////////////////////////////////////////////////////////////////////////
// class AutoInvokable/AutoInvoker - 自动被调对象/自动调用者
//
// 说明:
// 1. 这两个类联合使用，可以起到和 "智能指针" 类似的作用，即利用栈对象自动销毁的特性，在栈
//    对象的生命周期中自动调用 AutoInvokable::invokeInitialize() 和 invokeFinalize()。
//    此二类一般使用在重要资源的对称性操作场合(比如加锁/解锁)。
// 2. 使用者需继承 AutoInvokable 类，重写 invokeInitialize() 和 invokeFinalize()
//    函数。并在需要调用的地方定义 AutoInvoker 的栈对象。

class AutoInvokable
{
public:
    friend class AutoInvoker;
    virtual ~AutoInvokable() {}
protected:
    virtual void invokeInitialize() {}
    virtual void invokeFinalize() {}
};

class AutoInvoker : boost::noncopyable
{
public:
    explicit AutoInvoker(AutoInvokable& object)
        { object_ = &object; object_->invokeInitialize(); }

    explicit AutoInvoker(AutoInvokable *object)
        { object_ = object; if (object_) object_->invokeInitialize(); }

    virtual ~AutoInvoker()
        { if (object_) object_->invokeFinalize(); }

private:
    AutoInvokable *object_;
};

///////////////////////////////////////////////////////////////////////////////
// class AutoLocker - 线程自动互斥类
//
// 说明:
// 1. 此类利用C++的栈对象自动销毁的特性，在多线程环境下进行局部范围临界区互斥；
// 2. 使用方法: 在需要互斥的范围中以局部变量方式定义此类对象即可；
//
// 使用范例:
//   假设已定义: Mutex mutex_;
//   自动加锁和解锁:
//   {
//       AutoLocker locker(mutex_);
//       //...
//   }

typedef AutoInvoker AutoLocker;

///////////////////////////////////////////////////////////////////////////////
// class BaseMutex - 互斥器基类

class BaseMutex:
    public AutoInvokable,
    boost::noncopyable
{
public:
    virtual void lock() = 0;
    virtual void unlock() = 0;
protected:
    void invokeInitialize() override { lock(); }
    void invokeFinalize() override { unlock(); }
};

///////////////////////////////////////////////////////////////////////////////
// class Mutex - 线程互斥类
//
// 说明:
// 1. 此类用于多线程环境下临界区互斥，基本操作有 lock、unlock；
// 2. 线程内允许嵌套调用 lock，嵌套调用后必须调用相同次数的 unlock 才可解锁；

class Mutex : public BaseMutex
{
public:
    Mutex();
    ~Mutex() override;

    void lock() override;
    void unlock() override;

private:
#ifdef ISE_WINDOWS
    CRITICAL_SECTION critiSect_;
#endif
#ifdef ISE_LINUX
    // 嵌套加锁是这个类对外承诺的语义(见上面的说明 2)，所以是 recursive_mutex
    // 而不是 mutex。
    std::recursive_mutex mutex_;
    friend class Condition;
#endif
};

///////////////////////////////////////////////////////////////////////////////
// class Semaphore - 信号量类

class Semaphore : boost::noncopyable
{
public:
    explicit Semaphore(UINT initValue = 0);
    virtual ~Semaphore();

    void increase();
    void wait();
    void reset();

private:
    void doCreateSem();
    void doDestroySem();

private:
#ifdef ISE_WINDOWS
    HANDLE sem_;
    friend class Condition;
#endif
#ifdef ISE_LINUX
    sem_t sem_;
#endif

    UINT initValue_;
};

///////////////////////////////////////////////////////////////////////////////
// class Condition - 条件变量类
//
// 说明:
// * 条件变量的使用总是和一个互斥锁 (Condition::Mutex) 结合在一起。
// * 由于 ise::Mutex 类的 Windows 实现采用了非内核对象的 CRITICAL_SECTION，故不能
//   与 Condition 配合使用。为了统一，无论 Linux 平台还是 Windows 平台，都一律使用
//   Condition::Mutex 与 Condition 搭配。
// * 调用 wait() 时，会原子地 unlock mutex 并进入等待，执行完毕时会自动重新 lock mutex.
// * 应该采用循环方式调用 wait()，以防虚假唤醒 (spurious wakeup)。
// * 典型使用方式是:
//
//   class Example
//   {
//   public:
//       Example() : condition_(mutex_) {}
//
//       void addToQueue()
//       {
//           {
//               AutoLocker locker(mutex_);
//               queue_.push_back(...);
//           }
//           condition_.notify();
//       }
//
//       void extractFromQueue()
//       {
//           AutoLocker locker(mutex_);
//           while (queue_.empty())
//               condition_.wait();
//           assert(!queue_.empty());
//
//           int top = queue_.front();
//           queue_.pop_front();
//           ...
//       }
//
//   private:
//       Condition::Mutex mutex_;
//       Condition condition_;
//   };

class Condition : boost::noncopyable
{
public:
    class Mutex : public BaseMutex
    {
    public:
        Mutex();
        ~Mutex() override;

        void lock() override;
        void unlock() override;

    private:
#ifdef ISE_WINDOWS
        HANDLE mutex_;
#endif
#ifdef ISE_LINUX
        ise::Mutex mutex_;
#endif
        friend class Condition;
    };

public:
    Condition(Condition::Mutex& mutex);
    ~Condition();

    void wait();
    void notify();
    void notifyAll();

    Condition::Mutex& getMutex() { return mutex_; }

private:
    Condition::Mutex& mutex_;

#ifdef ISE_WINDOWS
    Semaphore sem_;
    boost::scoped_ptr<AtomicInt> waiters_;
#endif

#ifdef ISE_LINUX
    // condition_variable 只能等 std::unique_lock<std::mutex>；此处要等的是
    // Condition::Mutex，它只提供 lock/unlock，故用 condition_variable_any。
    std::condition_variable_any cond_;
#endif
};

///////////////////////////////////////////////////////////////////////////////
// class SignalMasker - 信号屏蔽类

#ifdef ISE_LINUX
class SignalMasker : boost::noncopyable
{
public:
    explicit SignalMasker(bool isAutoRestore = false);
    virtual ~SignalMasker();

    // 设置 Block/UnBlock 操作所需的信号集合。
    // 从前的第一个参数是"后面还有几个", 一个只有调用者知道、编译器从不核对的
    // 数字。现在没有这个参数: 参数包自己知道自己有多长。
    template <std::integral... Sigs>
    void setSignals(Sigs... sigs)
    {
        sigemptyset(&newSet_);
        (..., sigaddset(&newSet_, static_cast<int>(sigs)));
    }

    // 在进程当前阻塞信号集中添加 setSignals 设置的信号
    void block();
    // 在进程当前阻塞信号集中解除 setSignals 设置的信号
    void unBlock();

    // 将进程阻塞信号集恢复为 Block/UnBlock 之前的状态
    void restore();

private:
    int sigProcMask(int how, const sigset_t *newSet, sigset_t *oldSet);

private:
    sigset_t oldSet_;
    sigset_t newSet_;
    bool isBlock_;
    bool isAutoRestore_;
};
#endif

///////////////////////////////////////////////////////////////////////////////
// class AtomicInteger - 提供原子操作的整数类

#ifdef ISE_WINDOWS

class AtomicInt : boost::noncopyable
{
public:
    AtomicInt() : value_(0) {}

    LONG get() { return InterlockedCompareExchange(&value_, 0, 0); }
    LONG set(LONG newValue) { return InterlockedExchange(&value_, newValue); }
    LONG getAndAdd(LONG x) { return InterlockedExchangeAdd(&value_, x); }
    LONG addAndGet(LONG x) { return getAndAdd(x) + x; }
    LONG increment() { return InterlockedIncrement(&value_); }
    LONG decrement() { return InterlockedDecrement(&value_); }
    LONG getAndSet(LONG newValue) { return set(newValue); }

private:
    volatile LONG value_;
};

class AtomicInt64 : boost::noncopyable
{
public:
    AtomicInt64() : value_(0) {}

    INT64 get()
    {
        AutoLocker locker(mutex_);
        return value_;
    }
    INT64 set(INT64 newValue)
    {
        AutoLocker locker(mutex_);
        INT64 temp = value_;
        value_ = newValue;
        return temp;
    }
    INT64 getAndAdd(INT64 x)
    {
        AutoLocker locker(mutex_);
        INT64 temp = value_;
        value_ += x;
        return temp;
    }
    INT64 addAndGet(INT64 x)
    {
        AutoLocker locker(mutex_);
        value_ += x;
        return value_;
    }
    INT64 increment()
    {
        AutoLocker locker(mutex_);
        ++value_;
        return value_;
    }
    INT64 decrement()
    {
        AutoLocker locker(mutex_);
        --value_;
        return value_;
    }
    INT64 getAndSet(INT64 newValue)
    {
        return set(newValue);
    }

private:
    volatile INT64 value_;
    Mutex mutex_;
};

#endif

#ifdef ISE_LINUX

template<std::signed_integral T>
class AtomicInteger : boost::noncopyable
{
public:
    AtomicInteger() : value_(0) {}

    [[nodiscard]] T get() { return value_.load(); }
    T set(T newValue) { return value_.exchange(newValue); }
    T getAndAdd(T x) { return value_.fetch_add(x); }
    T addAndGet(T x) { return value_.fetch_add(x) + x; }
    T increment() { return addAndGet(1); }
    T decrement() { return addAndGet(-1); }
    T getAndSet(T newValue) { return set(newValue); }

private:
    // volatile 从来不是同步原语: 它不保证原子性，也不排序别的访问。
    // std::atomic 是这段代码一直想要的那个东西。
    std::atomic<T> value_;
};

class AtomicInt : public AtomicInteger<long> {};
class AtomicInt64 : public AtomicInteger<INT64> {};

#endif

///////////////////////////////////////////////////////////////////////////////
// class SeqNumberAlloc - 整数序列号分配器类
//
// 说明:
// 1. 此类以线程安全方式生成一个不断递增的整数序列，用户可以指定序列的起始值；
// 2. 此类一般用于数据包的顺序号控制；

class SeqNumberAlloc : boost::noncopyable
{
public:
    explicit SeqNumberAlloc(UINT64 startId = 0);

    // 返回一个新分配的ID
    UINT64 allocId();

private:
    Mutex mutex_;
    UINT64 currentId_;
};

///////////////////////////////////////////////////////////////////////////////
// class Stream - 流 基类

enum class SEEK_ORIGIN
{
    SO_BEGINNING    = 0,
    SO_CURRENT      = 1,
    SO_END          = 2
};

class Stream
{
public:
    virtual ~Stream() {}

    virtual int read(void *buffer, int count) = 0;
    virtual int write(const void *buffer, int count) = 0;
    virtual INT64 seek(INT64 offset, SEEK_ORIGIN seekOrigin) = 0;

    void readBuffer(void *buffer, int count);
    void writeBuffer(const void *buffer, int count);

    INT64 getPosition() { return seek(0, SEEK_ORIGIN::SO_CURRENT); }
    void setPosition(INT64 pos) { seek(pos, SEEK_ORIGIN::SO_BEGINNING); }

    virtual INT64 getSize();
    virtual void setSize(INT64 /*size*/) {}
};

///////////////////////////////////////////////////////////////////////////////
// class MemoryStream - 内存流类

class MemoryStream : public Stream
{
public:
    enum { DEFAULT_MEMORY_DELTA = 1024 };    // 缺省内存增长步长 (字节数，必须是 2 的 N 次方)
    enum { MIN_MEMORY_DELTA = 256 };         // 最小内存增长步长

public:
    explicit MemoryStream(int memoryDelta = DEFAULT_MEMORY_DELTA);
    MemoryStream(const MemoryStream& src);
    MemoryStream(MemoryStream&& src);
    ~MemoryStream() override;

    MemoryStream& operator = (const MemoryStream& rhs);
    MemoryStream& operator = (MemoryStream&& rhs);

    int read(void *buffer, int count) override;
    int write(const void *buffer, int count) override;
    INT64 seek(INT64 offset, SEEK_ORIGIN seekOrigin) override;
    void setSize(INT64 size) override;
    bool loadFromStream(Stream& stream);
    bool loadFromFile(const string& fileName);
    bool saveToStream(Stream& stream);
    bool saveToFile(const string& fileName);
    void clear();
    char* getMemory() { return memory_; }

private:
    void init();
    void assign(const MemoryStream& src);
    void setMemoryDelta(int newMemoryDelta);
    void setPointer(char *memory, int size);
    void setCapacity(int newCapacity);
    char* realloc(int& newCapacity);

private:
    char *memory_;
    int capacity_;
    int size_;
    int position_;
    int memoryDelta_;
};

///////////////////////////////////////////////////////////////////////////////
// class FileStream - 文件流类

// 文件流打开方式 (UINT openMode)
#ifdef ISE_WINDOWS
enum
{
    FM_CREATE           = 0xFFFF,
    FM_OPEN_READ        = 0x0000,
    FM_OPEN_WRITE       = 0x0001,
    FM_OPEN_READ_WRITE  = 0x0002,

    FM_SHARE_EXCLUSIVE  = 0x0010,
    FM_SHARE_DENY_WRITE = 0x0020,
    FM_SHARE_DENY_NONE  = 0x0040
};
#endif
#ifdef ISE_LINUX
enum
{
    FM_CREATE           = 0xFFFF,
    FM_OPEN_READ        = O_RDONLY,  // 0
    FM_OPEN_WRITE       = O_WRONLY,  // 1
    FM_OPEN_READ_WRITE  = O_RDWR,    // 2

    FM_SHARE_EXCLUSIVE  = 0x0010,
    FM_SHARE_DENY_WRITE = 0x0020,
    FM_SHARE_DENY_NONE  = 0x0030
};
#endif

// 缺省文件存取权限 (rights)
#ifdef ISE_WINDOWS
enum { DEFAULT_FILE_ACCESS_RIGHTS = 0 };
#endif
#ifdef ISE_LINUX
enum { DEFAULT_FILE_ACCESS_RIGHTS = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH };
#endif

class FileStream :
    public Stream,
    boost::noncopyable
{
public:
    FileStream();
    FileStream(const string& fileName, UINT openMode, UINT rights = DEFAULT_FILE_ACCESS_RIGHTS);
    ~FileStream() override;

    bool open(const string& fileName, UINT openMode,
        UINT rights = DEFAULT_FILE_ACCESS_RIGHTS, FileException* exception = nullptr);
    void close();

    int read(void *buffer, int count) override;
    int write(const void *buffer, int count) override;
    INT64 seek(INT64 offset, SEEK_ORIGIN seekOrigin) override;
    void setSize(INT64 size) override;

    string getFileName() const { return fileName_; }
    HANDLE getHandle() const { return handle_; }
    bool isOpen() const;

private:
    void init();
    HANDLE fileCreate(const string& fileName, UINT rights);
    HANDLE fileOpen(const string& fileName, UINT openMode);
    void fileClose(HANDLE handle);
    int fileRead(HANDLE handle, void *buffer, int count);
    int fileWrite(HANDLE handle, const void *buffer, int count);
    INT64 fileSeek(HANDLE handle, INT64 offset, SEEK_ORIGIN seekOrigin);

private:
    string fileName_;
    HANDLE handle_;
};

///////////////////////////////////////////////////////////////////////////////
// class PointerList - 列表类
//
// 说明:
// 1. 此类的实现原理与 Delphi::TList 完全相同；
// 2. 此列表类有如下优点:
//    a. 公有方法简单明确 (STL虽无比强大但稍显晦涩)；
//    b. 支持下标随机存取各个元素 (STL::list不支持)；
//    c. 支持快速获取列表长度 (STL::list不支持)；
//    d. 支持尾部快速增删元素；
// 3. 此列表类有如下缺点:
//    a. 不支持头部和中部的快速增删元素；
//    b. 只支持单一类型元素(Pointer类型)；

class PointerList
{
public:
    PointerList();
    // 今天的拷贝构造函数是编译器生成的(逐成员浅拷贝)，而声明移动构造函数会把它
    // 取消掉。此处显式保留: 本步骤给这些类第二种转移方式，不收回第一种。
    PointerList(const PointerList& src) = default;
    PointerList(PointerList&& src);
    virtual ~PointerList();

    int add(POINTER item);
    void insert(int index, POINTER item);
    void del(int index);
    int remove(POINTER item);
    POINTER extract(POINTER item);
    void move(int curIndex, int newIndex);
    void resize(int count);
    void clear();

    POINTER first() const;
    POINTER last() const;
    int indexOf(POINTER item) const;
    int getCount() const;
    bool isEmpty() const { return (getCount() <= 0); }

    PointerList& operator = (const PointerList& rhs);
    PointerList& operator = (PointerList&& rhs);
    const POINTER& operator[] (int index) const;
    POINTER& operator[] (int index);

protected:
    virtual void grow();

    POINTER get(int index) const;
    void put(int index, POINTER item);
    void setCapacity(int newCapacity);
    void setCount(int newCount);

private:
    POINTER *list_;
    int count_;
    int capacity_;
};

///////////////////////////////////////////////////////////////////////////////
// class PropertyList - 属性列表类
//
// 说明:
// 1. 属性列表中的每个项目由属性名(Name)和属性值(Value)组成。
// 2. 属性名不可重复，不区分大小写，且其中不可含有等号"="。属性值可为任意值。

class PropertyList
{
public:
    enum { NAME_VALUE_SEP = '=' };        // Name 和 Value 之间的分隔符
    enum { PROP_ITEM_SEP  = ',' };        // 属性项目之间的分隔符
    enum { PROP_ITEM_QUOT = '"' };

    struct PropertyItem
    {
        string name, value;
    public:
        PropertyItem(const string& _name, const string& _value) :
            name(_name), value(_value) {}
    };

public:
    PropertyList();
    PropertyList(const PropertyList& src);
    PropertyList(PropertyList&& src) = default;
    virtual ~PropertyList();

    void add(const string& name, const string& value);
    bool remove(const string& name);
    void clear();
    int indexOf(const string& name) const;
    bool nameExists(const string& name) const;
    bool getValue(const string& name, string& value) const;
    int getCount() const { return static_cast<int>(items_.size()); }
    bool isEmpty() const { return (getCount() <= 0); }
    const PropertyItem& getItem(int index) const;
    string getPropString() const;
    void setPropString(const string& propString);

    PropertyList& operator = (const PropertyList& rhs);
    PropertyList& operator = (PropertyList&& rhs) = default;
    string& operator[] (const string& name);

private:
    void assign(const PropertyList& src);
    PropertyItem* find(const string& name);
    static bool isReservedChar(char ch);
    static bool hasReservedChar(const string& str);
    static char* scanStr(char *str, char ch);
    static string makeQuotedStr(const string& str);
    static string extractQuotedStr(char*& strPtr);

private:
    std::vector<PropertyItem> items_;          // 属性项目，按添加顺序排列
};

///////////////////////////////////////////////////////////////////////////////
// class Strings - 字符串列表抽象类

class Strings
{
private:
    enum class STRINGS_DEFINED : UINT
    {
        SD_DELIMITER         = 0x0001,
        SD_QUOTE_CHAR        = 0x0002,
        SD_NAME_VALUE_SEP    = 0x0004,
        SD_LINE_BREAK        = 0x0008
    };

public:
    // Calls beginUpdate() and endUpdate() automatically in a scope.
    class AutoUpdater
    {
    private:
        Strings& strings;
    public:
        AutoUpdater(Strings& _strings) : strings(_strings)
            { strings.beginUpdate(); }
        ~AutoUpdater()
            { strings.endUpdate(); }
    };

public:
    Strings();
    // 与 PointerList 同理: 显式保留编译器生成的拷贝构造函数。
    Strings(const Strings& src) = default;
    Strings(Strings&& src) = default;
    virtual ~Strings() {}

    virtual int add(const char* str);
    virtual int add(const char* str, POINTER data);
    virtual void addStrings(const Strings& strings);
    virtual void insert(int index, const char* str) = 0;
    virtual void insert(int index, const char* str, POINTER data);
    virtual void clear() = 0;
    virtual void del(int index) = 0;
    virtual bool equals(const Strings& strings);
    virtual void exchange(int index1, int index2);
    virtual void move(int curIndex, int newIndex);
    virtual bool exists(const char* str) const;
    virtual int indexOf(const char* str) const;
    virtual int indexOfName(const char* name) const;
    virtual int indexOfData(POINTER data) const;

    virtual bool loadFromStream(Stream& stream);
    virtual bool loadFromFile(const char* fileName);
    virtual bool saveToStream(Stream& stream) const;
    virtual bool saveToFile(const char* fileName) const;

    virtual int getCapacity() const { return getCount(); }
    virtual void setCapacity(int /*value*/) {}
    virtual int getCount() const = 0;
    bool isEmpty() const { return (getCount() <= 0); }
    char getDelimiter() const;
    void setDelimiter(char value);
    string getLineBreak() const;
    void setLineBreak(const char* value);
    char getQuoteChar() const;
    void setQuoteChar(char value);
    char getNameValueSeparator() const;
    void setNameValueSeparator(char value);
    string combineNameValue(const char* name, const char* value) const;
    string getName(int index) const;
    string getValue(const char* name) const;
    string getValue(int index) const;
    void setValue(const char* name, const char* value);
    void setValue(int index, const char* value);
    virtual POINTER getData(int /*index*/) const { return nullptr; }
    virtual void setData(int /*index*/, POINTER /*data*/) {}
    virtual string getText() const;
    virtual void setText(const char* value);
    string getCommaText() const;
    void setCommaText(const char* value);
    string getDelimitedText() const;
    void setDelimitedText(const char* value);
    virtual const string& getString(int index) const = 0;
    virtual void setString(int index, const char* value);

    void beginUpdate();
    void endUpdate();

    Strings& operator = (const Strings& rhs);
    Strings& operator = (Strings&& rhs) = default;
    const string& operator[] (int index) const { return getString(index); }

protected:
    virtual void setUpdateState(bool /*isUpdating*/) {}
    virtual int compareStrings(const char* str1, const char* str2) const;

protected:
    void init();
    void error(const char* msg, int data) const;
    int getUpdateCount() const { return updateCount_; }
    string extractName(const char* str) const;

private:
    void assign(const Strings& src);

protected:
    UINT defined_;
    char delimiter_;
    string lineBreak_;
    char quoteChar_;
    char nameValueSeparator_;
    int updateCount_;
};

///////////////////////////////////////////////////////////////////////////////
// class StrList - 字符串列表类

class StrList : public Strings
{
public:
    friend int stringListCompareProc(const StrList& stringList, int index1, int index2);

public:
    /// The comparison function prototype that used by Sort().
    typedef int (*StringListCompareProc)(const StrList& stringList, int index1, int index2);

    /// Indicates the response when an application attempts to add a duplicate entry to a list.
    enum class DUPLICATE_MODE
    {
        DM_IGNORE,      // Ignore attempts to add duplicate entries (do not add the duplicate).
        DM_ACCEPT,      // Allow the list to contain duplicate entries (add the duplicate).
        DM_ERROR        // Throw an exception when the application tries to add a duplicate.
    };

public:
    StrList();
    StrList(const StrList& src);
    StrList(StrList&& src) = default;
    ~StrList() override;

    int add(const char* str) override;
    int add(const char* str, POINTER data) override;
    int add(const string& str) { return add(str.c_str()); }
    void clear() override;
    void del(int index) override;
    void exchange(int index1, int index2) override;
    int indexOf(const char* str) const override;
    void insert(int index, const char* str) override;
    void insert(int index, const char* str, POINTER data) override;

    int getCapacity() const override { return static_cast<int>(list_.size()); }
    void setCapacity(int value) override;
    int getCount() const override { return count_; }
    POINTER getData(int index) const override;
    void setData(int index, POINTER data) override;
    const string& getString(int index) const override;
    void setString(int index, const char* value) override;

    virtual bool find(const char* str, int& index) const;
    virtual void sort();
    virtual void sort(StringListCompareProc compareProc);

    DUPLICATE_MODE getDupMode() const { return dupMode_; }
    void setDupMode(DUPLICATE_MODE value) { dupMode_ = value; }
    bool getSorted() const { return isSorted_; }
    virtual void setSorted(bool value);
    bool getCaseSensitive() const { return isCaseSensitive_; }
    virtual void setCaseSensitive(bool value);

    StrList& operator = (const StrList& rhs);
    StrList& operator = (StrList&& rhs) = default;

protected: // override
    void setUpdateState(bool isUpdating) override;
    int compareStrings(const char* str1, const char* str2) const override;

protected: // virtual
    /// Occurs immediately before the list of strings changes.
    virtual void onChanging() {}
    /// Occurs immediately after the list of strings changes.
    virtual void onChanged() {}
    /// Internal method used to insert a string to the list.
    virtual void insertItem(int index, const char* str, POINTER data);

private:
    void init();
    void assign(const StrList& src);
    void internalClear();
    void exchangeItems(int index1, int index2);
    void grow();
    void quickSort(int l, int r, StringListCompareProc compareProc);

private:
    struct StringItem
    {
        string str;
        POINTER data = nullptr;
    };

private:
    std::vector<StringItem> list_;      // 长度即容量，前 count_ 项为已用项
    int count_;
    DUPLICATE_MODE dupMode_;
    bool isSorted_;
    bool isCaseSensitive_;
};

///////////////////////////////////////////////////////////////////////////////
// class Url - URL解析类

class Url
{
public:
    // The URL parts.
    enum class URL_PART : UINT
    {
        URL_PROTOCOL  = 0x0001,
        URL_HOST      = 0x0002,
        URL_PORT      = 0x0004,
        URL_PATH      = 0x0008,
        URL_FILENAME  = 0x0010,
        URL_BOOKMARK  = 0x0020,
        URL_USERNAME  = 0x0040,
        URL_PASSWORD  = 0x0080,
        URL_PARAMS    = 0x0100,
        URL_ALL       = 0xFFFF,
    };

public:
    Url(const string& url = "");
    Url(const Url& src);
    Url(Url&& src) = default;
    virtual ~Url() {}

    void clear();
    Url& operator = (const Url& rhs);
    Url& operator = (Url&& rhs) = default;

    string getUrl() const;
    string getUrl(UINT parts);
    void setUrl(const string& value);

    const string& getProtocol() const { return protocol_; }
    const string& getHost() const { return host_; }
    const string& getPort() const { return port_; }
    const string& getPath() const { return path_; }
    const string& getFileName() const { return fileName_; }
    const string& getBookmark() const { return bookmark_; }
    const string& getUserName() const { return userName_; }
    const string& getPassword() const { return password_; }
    const string& getParams() const { return params_; }

    void setProtocol(const string& value) { protocol_ = value; }
    void setHost(const string& value) { host_ = value; }
    void setPort(const string& value) { port_ = value; }
    void setPath(const string& value) { path_ = value; }
    void setFileName(const string& value) { fileName_ = value; }
    void setBookmark(const string& value) { bookmark_ = value; }
    void setUserName(const string& value) { userName_ = value; }
    void setPassword(const string& value) { password_ = value; }
    void setParams(const string& value) { params_ = value; }

private:
    string protocol_;
    string host_;
    string port_;
    string path_;
    string fileName_;
    string bookmark_;
    string userName_;
    string password_;
    string params_;
};

///////////////////////////////////////////////////////////////////////////////
// class Packet - 数据包基类

class Packet
{
public:
    // 缺省内存增长步长 (字节数，必须是 2 的 N 次方)
    enum { DEFAULT_MEMORY_DELTA = 1024 };

public:
    Packet();
    virtual ~Packet();

    bool pack();
    bool unpack(void *buffer, int bytes);
    bool unpack(const Buffer& buffer);
    void clear();
    void ensurePacked();

    char* getBuffer() const { return (stream_? static_cast<char*>(stream_->getMemory()) : nullptr); }
    int getSize() const { return (stream_? static_cast<int>(stream_->getSize()) : 0); }
    bool isAvailable() const { return isAvailable_; }
    bool IsPacked() const { return isPacked_; }

protected:
    void throwUnpackError();
    void throwPackError();
    void checkUnsafeSize(int value);

    void readBuffer(void *buffer, int bytes);
    void readINT8(INT8& value) { readBuffer(&value, sizeof(INT8)); }
    void readINT16(INT16& value) { readBuffer(&value, sizeof(INT16)); }
    void readINT32(INT32& value) { readBuffer(&value, sizeof(INT32)); }
    void readINT64(INT64& value) { readBuffer(&value, sizeof(INT64)); }
    void readString(string& str);
    void readBlob(string& str);
    void readBlob(Stream& stream);
    void readBlob(Buffer& buffer);
    INT8 readINT8() { INT8 v; readINT8(v); return v; }
    INT16 readINT16() { INT16 v; readINT16(v); return v; }
    INT32 readINT32() { INT32 v; readINT32(v); return v; }
    INT64 readINT64() { INT64 v; readINT64(v); return v; }
    bool readBool() { INT8 v; readINT8(v); return (v? true : false); }
    string readString() { string v; readString(v); return v; }

    void writeBuffer(const void *buffer, int bytes);
    void writeINT8(const INT8& value) { writeBuffer(&value, sizeof(INT8)); }
    void writeINT16(const INT16& value) { writeBuffer(&value, sizeof(INT16)); }
    void writeINT32(const INT32& value) { writeBuffer(&value, sizeof(INT32)); }
    void writeINT64(const INT64& value) { writeBuffer(&value, sizeof(INT64)); }
    void writeBool(bool value) { writeINT8(value ? 1 : 0); }
    void writeString(const string& str);
    void writeBlob(void *buffer, int bytes);
    void writeBlob(const Buffer& buffer);

    void fixStrLength(string& str, int length);
    void truncString(string& str, int maxLength);

protected:
    virtual void doPack() {}
    virtual void doUnpack() {}
    virtual void doAfterPack() {}
    virtual void doEncrypt() {}
    virtual void doDecrypt() {}
    virtual void doCompress() {}
    virtual void doDecompress() {}

private:
    void init();

protected:
    std::unique_ptr<MemoryStream> stream_;
    bool isAvailable_;
    bool isPacked_;
};

///////////////////////////////////////////////////////////////////////////////
// class CustomObjectList - 对象列表基类

template<OwnedObject ObjectType>
class CustomObjectList
{
public:
    class InternalMutex : public BaseMutex
    {
    public:
        InternalMutex(bool active) { if (active) mutex_.reset(new Mutex()); }
        // 列表可以被移动，它自带的这把锁就得跟着走。被移走的一方此后不再加锁，
        // 与它被移走的其它成员一样，只保证可析构、可被赋值。
        // 不写这两个，CustomObjectList 的 `= default` 移动会解析成 deleted:
        // unique_ptr 不可拷贝，boost::noncopyable 也不给移动。
        InternalMutex(InternalMutex&& src) : mutex_(std::move(src.mutex_)) {}
        InternalMutex& operator = (InternalMutex&& rhs)
            { mutex_ = std::move(rhs.mutex_); return *this; }
        ~InternalMutex() override {}
    public:
        void lock() override { if (mutex_) mutex_->lock(); }
        void unlock() override { if (mutex_) mutex_->unlock(); }
    private:
        std::unique_ptr<Mutex> mutex_;
    };

    typedef ObjectType* ObjectPtr;

    // 元素的删除器: 列表不持有元素时什么也不做，元素只是登记在此。
    // 是否持有元素是每个元素自带的属性，故 setOwnsObjects() 改的是各元素的删除器。
    class ObjectDeleter
    {
    public:
        explicit ObjectDeleter(bool isOwnsObject = true) : isOwnsObject_(isOwnsObject) {}
        void operator() (ObjectPtr object) const
        {
            if (isOwnsObject_)
                std::default_delete<ObjectType>()(object);
        }
    private:
        bool isOwnsObject_;
    };

    // 元素的句柄，列表中的每个元素都由它持有(或只是被它登记)。
    typedef std::unique_ptr<ObjectType, ObjectDeleter> ObjectHandle;

public:
    CustomObjectList() :
        mutex_(false), isOwnsObjects_(true) {}

    CustomObjectList(bool isThreadSafe, bool isOwnsObjects) :
        mutex_(isThreadSafe), isOwnsObjects_(isOwnsObjects) {}

    // 拷贝构造今天就是 deleted 的(元素句柄是 unique_ptr)，无须保留。
    CustomObjectList(CustomObjectList&& src) = default;

    virtual ~CustomObjectList() { clear(); }

protected:
    // 销毁第 index 个元素持有的对象。是否真的销毁由该元素自带的删除器决定，
    // 与 C++98 版本里对 isOwnsObjects_ 的判断等价。remove()、del() 和 clear()
    // 都经由它删除元素，子类改写它即改写这三者的含义 —— 这正是它是虚函数的理由。
    virtual void notifyDelete(int index)
    {
        items_[index].reset();
    }

protected:
    int add(ObjectPtr item, bool allowDuplicate = true)
    {
        ISE_ASSERT(item);
        AutoLocker locker(mutex_);

        if (!allowDuplicate && indexOfItem(item) >= 0)
            return -1;

        // item 的所有权在此处交给列表 (列表不持有元素时，只是把它登记下来)
        items_.push_back(ObjectHandle(item, ObjectDeleter(isOwnsObjects_)));
        return static_cast<int>(items_.size()) - 1;
    }

    int remove(ObjectPtr item)
    {
        AutoLocker locker(mutex_);

        int result = indexOfItem(item);
        if (result >= 0)
        {
            notifyDelete(result);
            items_.erase(items_.begin() + result);
        }
        return result;
    }

    ObjectPtr extract(ObjectPtr item)
    {
        AutoLocker locker(mutex_);
        return extractAt(indexOfItem(item));
    }

    ObjectPtr extract(int index)
    {
        AutoLocker locker(mutex_);
        return extractAt(index);
    }

    void del(int index)
    {
        AutoLocker locker(mutex_);

        if (index >= 0 && index < static_cast<int>(items_.size()))
        {
            notifyDelete(index);
            items_.erase(items_.begin() + index);
        }
    }

    void insert(int index, ObjectPtr item)
    {
        ISE_ASSERT(item);
        AutoLocker locker(mutex_);

        if (index < 0 || index > static_cast<int>(items_.size()))
            iseThrowException(SEM_INDEX_ERROR);

        items_.insert(items_.begin() + index, ObjectHandle(item, ObjectDeleter(isOwnsObjects_)));
    }

    int indexOf(ObjectPtr item) const
    {
        AutoLocker locker(mutex_);
        return indexOfItem(item);
    }

    bool exists(ObjectPtr item) const
    {
        AutoLocker locker(mutex_);
        return indexOfItem(item) >= 0;
    }

    ObjectPtr first() const
    {
        AutoLocker locker(mutex_);
        return getItem(0);
    }

    ObjectPtr last() const
    {
        AutoLocker locker(mutex_);
        return getItem(getCount() - 1);
    }

    void clear()
    {
        AutoLocker locker(mutex_);

        // 与 C++98 版本一致：先逆序逐个通知销毁，列表的长度在这期间不变，
        // 改写 notifyDelete() 的子类在其中读到的 getCount() 因而与从前相同；
        // 全部销毁之后列表才清空。
        for (int i = static_cast<int>(items_.size()) - 1; i >= 0; i--)
            notifyDelete(i);
        items_.clear();
    }

    // 与 C++98 版本一致：无论列表是否持有元素，每个元素都在此销毁，逆序销毁，
    // 而列表的长度不变 —— 销毁之后各元素为空。
    void freeObjects()
    {
        AutoLocker locker(mutex_);

        for (int i = static_cast<int>(items_.size()) - 1; i >= 0; i--)
            std::unique_ptr<ObjectType> owner(items_[i].release());
    }

    int getCount() const { return static_cast<int>(items_.size()); }
    ObjectPtr getItem(int index) const
    {
        if (index < 0 || index >= getCount())
            iseThrowException(SEM_INDEX_ERROR);
        return items_[index].get();
    }
    // 与 C++98 版本一致：这是一次浅拷贝。原有的元素只是被丢下而不销毁，赋值
    // 之后两个列表登记着同一批对象。是否持有它们，各自按各自的策略。
    CustomObjectList& operator = (const CustomObjectList& rhs)
    {
        if (this != &rhs)
        {
            for (size_t i = 0; i < items_.size(); i++)
                items_[i].release();
            items_.clear();
            for (size_t i = 0; i < rhs.items_.size(); i++)
                items_.push_back(ObjectHandle(rhs.items_[i].get(),
                    ObjectDeleter(isOwnsObjects_)));
        }
        return *this;
    }

    CustomObjectList& operator = (CustomObjectList&& rhs) = default;

    ObjectPtr operator[] (int index) const { return getItem(index); }
    bool isEmpty() const { return (getCount() <= 0); }

    // 改变所有权策略: 逐个换掉列表中各元素的删除器
    void setOwnsObjects(bool value)
    {
        AutoLocker locker(mutex_);

        isOwnsObjects_ = value;
        for (size_t i = 0; i < items_.size(); i++)
        {
            ObjectPtr item = items_[i].release();
            items_[i] = ObjectHandle(item, ObjectDeleter(value));
        }
    }

private:
    // 以下两个不加锁，供已持锁的成员函数调用
    int indexOfItem(ObjectPtr item) const
    {
        // 找的是"哪一个 handle 管着这个指针"。投影把"先取出 .get() 再比"
        // 写在一处, 循环体里就没有东西可写错了。
        auto found = std::ranges::find(items_, item, &ObjectHandle::get);
        return found == items_.end() ? -1 : static_cast<int>(found - items_.begin());
    }

    ObjectPtr extractAt(int index)
    {
        // 元素的所有权在此处从列表交还给调用者
        ObjectPtr result = nullptr;
        if (index >= 0 && index < static_cast<int>(items_.size()))
        {
            result = items_[index].release();
            items_.erase(items_.begin() + index);
        }
        return result;
    }

protected:
    std::vector<ObjectHandle> items_;   // 对象列表
    mutable InternalMutex mutex_;
    bool isOwnsObjects_;                // 元素被删除时，是否自动释放元素对象
};

///////////////////////////////////////////////////////////////////////////////
// class ObjectList - 对象列表类

template<OwnedObject ObjectType>
class ObjectList : public CustomObjectList<ObjectType>
{
public:
    ObjectList() :
        CustomObjectList<ObjectType>(false, true) {}
    ObjectList(bool isThreadSafe, bool isOwnsObjects) :
        CustomObjectList<ObjectType>(isThreadSafe, isOwnsObjects) {}
    ~ObjectList() override {}

    using CustomObjectList<ObjectType>::add;
    using CustomObjectList<ObjectType>::remove;
    using CustomObjectList<ObjectType>::extract;
    using CustomObjectList<ObjectType>::del;
    using CustomObjectList<ObjectType>::insert;
    using CustomObjectList<ObjectType>::indexOf;
    using CustomObjectList<ObjectType>::exists;
    using CustomObjectList<ObjectType>::first;
    using CustomObjectList<ObjectType>::last;
    using CustomObjectList<ObjectType>::clear;
    using CustomObjectList<ObjectType>::freeObjects;
    using CustomObjectList<ObjectType>::getCount;
    using CustomObjectList<ObjectType>::getItem;
    using CustomObjectList<ObjectType>::operator=;
    using CustomObjectList<ObjectType>::operator[];
    using CustomObjectList<ObjectType>::isEmpty;
    using CustomObjectList<ObjectType>::setOwnsObjects;
};

///////////////////////////////////////////////////////////////////////////////
// class CallbackList - 回调列表

template<EmptyTestable T>
class CallbackList
{
public:
    void registerCallback(const T& callback)
    {
        AutoLocker locker(mutex_);
        if (callback)
            items_.push_back(callback);
    }

    [[nodiscard]] int getCount() const { return static_cast<int>(items_.size()); }
    [[nodiscard]] const T& getItem(int index) const { return items_[index]; }

private:
    std::vector<T> items_;
    Mutex mutex_;
};

///////////////////////////////////////////////////////////////////////////////
// class BlockingQueue - 阻塞队列类

template<std::copy_constructible T>
class BlockingQueue : boost::noncopyable
{
public:
    BlockingQueue() : condition_(mutex_) {}

    void put(const T& item)
    {
        AutoLocker locker(mutex_);
        queue_.push_back(item);
        condition_.notify();
    }

    T take()
    {
        AutoLocker locker(mutex_);
        while (queue_.empty())
            condition_.wait();
        ISE_ASSERT(!queue_.empty());

        T item(queue_.front());
        queue_.pop_front();
        return item;
    }

    size_t getCount() const
    {
        AutoLocker locker(mutex_);
        return queue_.size();
    }

private:
    // mutable: getCount() is const and still has to take the lock. Clang
    // rejects the non-mutable spelling outright, at the point of definition
    // rather than of instantiation.
    mutable Condition::Mutex mutex_;
    Condition condition_;
    std::deque<T> queue_;
};

///////////////////////////////////////////////////////////////////////////////
// class ObjectContext - 从此类继承给对象添加上下文

class ObjectContext
{
public:
    void setContext(const std::any& value) { context_ = value; }
    const std::any& getContext() const { return context_; }
    std::any& getContext() { return context_; }
private:
    std::any context_;
};

///////////////////////////////////////////////////////////////////////////////
// class Singleton - 全局单例类

#ifdef ISE_WINDOWS

template<typename T>
class Singleton : boost::noncopyable
{
public:
    static T& instance()
    {
        // DCL with volatile
        if (instance_ == NULL)
        {
            AutoLocker locker(mutex_);
            if (instance_ == NULL)
                instance_ = new T();
            return *instance_;
        }
        return *instance_;
    }
protected:
    Singleton() {}
    ~Singleton() {}
private:
    static T* volatile instance_;
    static Mutex mutex_;
};

template<typename T> T* volatile Singleton<T>::instance_ = NULL;
template<typename T> Mutex Singleton<T>::mutex_;

#endif
#ifdef ISE_LINUX

template<typename T>
class Singleton : boost::noncopyable
{
public:
    // 约束写在 instance() 上而不是类模板参数上: Singleton<T> 作为 T 的基类
    // 被实例化时, T 还没有完整, 对一个不完整类型问这种问题是未定义的。
    static T& instance() requires Instantiable<T>
    {
        std::call_once(once_, &Singleton::init);
        return *instance_;
    }
protected:
    Singleton() {}
    ~Singleton() {}
private:
    static void init()
    {
        // 单例对象的生存期即进程的生存期: 它不在静态析构阶段销毁，因为其它对象的
        // 析构函数仍可能用到它(如 IseApplication::finalize() 要写日志)。所有权在
        // 此处从句柄交给进程本身。
        std::unique_ptr<T> instance(new T());
        instance_ = instance.release();
    }
private:
    static T* instance_;          // 仅指向单例对象，进程结束时随之消失
    static std::once_flag once_;
};

template<typename T> T* Singleton<T>::instance_ = nullptr;
template<typename T> std::once_flag Singleton<T>::once_;

#endif

///////////////////////////////////////////////////////////////////////////////
// class Logger - 日志类

class Logger : public Singleton<Logger>
{
public:
    void setFileName(const string& fileName, bool isNewFileDaily = false);

    void writeStr(const char *str);
    void writeStr(const string& str) { writeStr(str.c_str()); }

    // 格式串在编译期就被查过一遍: 占位符的个数或类型对不上, 报错指在
    // 调用处, 而不是等到运行时排版失败。std::format_string 是标准给出的
    // 这个约束; Args 这一边由 Formattable 挡住, 于是"这个类型没法排版"
    // 也是一句在调用处说出来的话。
    //
    // 可变参数约定管不着的东西现在可以直接传: std::string 不必再 .c_str(),
    // 一个自带 formatter 的类也不必先转成 const char *。唯一还写 .c_str()
    // 的地方是有意要在第一个 NUL 处停住 (见 examples/udp_server)。
    template <Formattable... Args>
    void writeFmt(std::format_string<Args...> format, Args&&... args)
        { writeStr(std::format(format, std::forward<Args>(args)...)); }
    void writeException(const Exception& e);

private:
    Logger();

private:
    string getLogFileName();
    bool openFile(FileStream& fileStream, const string& fileName);
    void writeToFile(const string& str);

private:
    string fileName_;       // 日志文件名
    bool isNewFileDaily_;   // 是否每天用一个单独的文件存储日志
    Mutex mutex_;

    friend class Singleton<Logger>;
};

///////////////////////////////////////////////////////////////////////////////
// 全局函数

inline Logger& logger() { return Logger::instance(); }

///////////////////////////////////////////////////////////////////////////////

} // namespace ise

#endif // _ISE_CLASSES_H_
