///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for the concurrency half of ise/main: Mutex,
// Condition, Semaphore, Thread, ThreadPool, Singleton, and the two time
// primitives every one of them is built on.
//
// Why this file exists at all. Every other file here characterises something
// the examples do not reach; this one characterises something they do reach
// but never *observe*. The servers start threads, take locks and sleep, and
// their transcripts say nothing about any of it -- so a migration could
// replace the whole synchronisation layer and the recorded behaviour would not
// move. The milestone that rewrites this layer therefore has to be scored
// almost entirely on the shape of the source, which is exactly the situation
// a characterisation driver exists to avoid.
//
// Determinism is the whole difficulty, and it is met the same way every time:
// worker threads never print. They put their results in memory, main joins
// them, and main prints. No case prints a thread id, an address, a duration or
// a scheduling order, because none of those is a function of the inputs.
//
// The one case that reads a clock, threads_tick_unit, prints a classification
// rather than a number: "how many milliseconds does the library think passed",
// bucketed so wide that only a change of *unit* can move it. That is
// deliberate -- the unit is a promise the library makes to getTickDiff's
// callers, and nothing else in the suite would notice if it changed.
//
// Two cases can hang rather than fail if the library is wrong, and that is the
// honest failure mode rather than a flaw here:
//   * threads_mutex_recursive takes the same lock twice from one thread. A
//     lock that does not permit that deadlocks; there is no way to observe
//     re-entrancy without re-entering.
//   * threads_condition_handoff waits for a notify that a broken condition
//     variable will not deliver.
// Both are bounded by the driver session's own timeout rather than by
// anything here, and a tree that hangs on either has broken the property the
// case is named after.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <cstddef>
#include <deque>
#include <string>

using namespace ise;
using drv::out;
using drv::outf;

namespace {

///////////////////////////////////////////////////////////////////////////////
// A Thread subclass whose work is supplied by a virtual, so each case can say
// what its threads do without repeating the boilerplate. Nothing here sets
// autoDelete: waitFor() asserts it is false, and every thread in this file is
// waited for by the case that started it.

class Worker : public Thread
{
public:
    Worker() : ran_(false) {}

    bool ran() const { return ran_; }

protected:
    virtual void work() = 0;

    virtual void execute()
    {
        work();
        ran_ = true;
    }

private:
    bool ran_;
};

// Runs `count` threads to completion and returns nothing; the caller reads
// whatever the workers left behind. Started in one pass and waited in another,
// so the threads genuinely overlap -- starting and waiting each in turn would
// make every case in this file a single-threaded one.
void runAll(Worker **workers, int count)
{
    for (int i = 0; i < count; i++)
        workers[i]->run();
    for (int i = 0; i < count; i++)
        workers[i]->waitFor();
}

///////////////////////////////////////////////////////////////////////////////
// Mutex

// Increments a plain, non-atomic counter under a lock. Plain on purpose: an
// atomic counter would come out right whether or not the lock works, and the
// number this case prints is the only evidence in the whole suite that the
// lock excludes anything.
class Counter : public Worker
{
public:
    Counter(Mutex& mutex, long& shared, int times) :
        mutex_(mutex), shared_(shared), times_(times) {}

protected:
    virtual void work()
    {
        for (int i = 0; i < times_; i++)
        {
            AutoLocker locker(mutex_);
            shared_ += 1;
        }
    }

private:
    Mutex& mutex_;
    long& shared_;
    int times_;
};

DRV_CASE(threads_mutex_excludes)
{
    const int THREADS = 4;
    const int TIMES = 20000;

    Mutex mutex;
    long shared = 0;

    Counter a(mutex, shared, TIMES);
    Counter b(mutex, shared, TIMES);
    Counter c(mutex, shared, TIMES);
    Counter d(mutex, shared, TIMES);
    Worker *workers[THREADS];
    workers[0] = &a; workers[1] = &b; workers[2] = &c; workers[3] = &d;

    runAll(workers, THREADS);

    outf("threads = %d, increments each = %d", THREADS, TIMES);
    outf("total = %s", intToStr(static_cast<INT64>(shared)).c_str());
    outf("every thread ran = %d",
        (a.ran() && b.ran() && c.ran() && d.ran()) ? 1 : 0);
}

// The lock this class documents may be taken again by the thread that holds
// it, and must then be released as many times as it was taken. Nothing else in
// the suite depends on that, and a lock without it deadlocks here rather than
// printing a wrong answer.
ISE_NOINLINE void innerLock(Mutex& mutex, long& depth)
{
    AutoLocker locker(mutex);
    depth += 1;
}

DRV_CASE(threads_mutex_recursive)
{
    Mutex mutex;
    long depth = 0;

    // Plain lock/unlock, nested by hand.
    mutex.lock();
    mutex.lock();
    depth += 1;
    mutex.unlock();
    mutex.unlock();
    outf("nested lock/unlock returned, depth = %s",
        intToStr(static_cast<INT64>(depth)).c_str());

    // The same thing through the scoped locker, which is how the project
    // itself re-enters: a locked region calls a helper that locks again.
    {
        AutoLocker outer(mutex);
        innerLock(mutex, depth);
        innerLock(mutex, depth);
    }
    outf("re-entered under AutoLocker, depth = %s",
        intToStr(static_cast<INT64>(depth)).c_str());

    // And once more after everything has been released, to show the lock is
    // not left owned by the unwinding above.
    {
        AutoLocker again(mutex);
        depth += 1;
    }
    outf("lock still usable afterwards, depth = %s",
        intToStr(static_cast<INT64>(depth)).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// Semaphore
//
// Only counts that are already available are waited for, so the case never
// blocks: this is a characterisation of the counter, not of the blocking.

DRV_CASE(threads_semaphore_counts)
{
    Semaphore sem(2);

    sem.wait();
    out("took 1 of 2");
    sem.wait();
    out("took 2 of 2");

    sem.increase();
    sem.increase();
    sem.wait();
    sem.wait();
    out("increased twice and took both");

    // reset() puts the count back to the value the semaphore was constructed
    // with, not to zero -- which is the whole reason the constructor argument
    // is remembered.
    sem.reset();
    sem.wait();
    sem.wait();
    out("after reset, two were available again");
}

// A thread held at the gate until its owner has finished setting it up: the
// use the library itself makes of this class.
class Gated : public Worker
{
public:
    Gated(Semaphore& gate, long& observed, const long& setUp) :
        gate_(gate), observed_(observed), setUp_(setUp) {}

protected:
    virtual void work()
    {
        gate_.wait();
        observed_ = setUp_;
    }

private:
    Semaphore& gate_;
    long& observed_;
    const long& setUp_;
};

DRV_CASE(threads_semaphore_gates_a_thread)
{
    Semaphore gate(0);
    long setUp = 0;
    long observed = -1;

    Gated worker(gate, observed, setUp);
    worker.run();

    // The thread is running and blocked on the gate; the value it will read is
    // written now, and only then is the gate opened. If the semaphore did not
    // hold it, this value would be racing the read.
    setUp = 4242;
    gate.increase();
    worker.waitFor();

    outf("thread observed = %s", intToStr(static_cast<INT64>(observed)).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// Condition
//
// The queue idiom the class's own comment documents, written exactly as that
// comment writes it: push under the lock, notify outside it, and wait in a
// loop against a predicate.

struct Handoff
{
    Condition::Mutex mutex;
    Condition condition;
    std::deque<int> queue;
    bool closed;

    Handoff() : condition(mutex), closed(false) {}
};

class Producer : public Worker
{
public:
    Producer(Handoff& handoff, int count) : handoff_(handoff), count_(count) {}

protected:
    virtual void work()
    {
        for (int i = 0; i < count_; i++)
        {
            {
                AutoLocker locker(handoff_.mutex);
                handoff_.queue.push_back(i * 10);
            }
            handoff_.condition.notify();
        }
        {
            AutoLocker locker(handoff_.mutex);
            handoff_.closed = true;
        }
        handoff_.condition.notifyAll();
    }

private:
    Handoff& handoff_;
    int count_;
};

DRV_CASE(threads_condition_handoff)
{
    const int COUNT = 5;

    Handoff handoff;
    Producer producer(handoff, COUNT);
    producer.run();

    std::string got;
    int taken = 0;
    while (taken < COUNT)
    {
        AutoLocker locker(handoff.mutex);
        while (handoff.queue.empty())
            handoff.condition.wait();
        while (!handoff.queue.empty())
        {
            if (!got.empty())
                got += " ";
            got += intToStr(static_cast<INT64>(handoff.queue.front()));
            handoff.queue.pop_front();
            taken += 1;
        }
    }

    producer.waitFor();

    // The order is the producer's, not the scheduler's: one producer pushing
    // in sequence into a FIFO. How many notifies it took to get here is not
    // printed, because that is the scheduler's answer.
    outf("received = %s", got.c_str());
    outf("count = %s", intToStr(static_cast<INT64>(taken)).c_str());
    outf("producer closed the queue = %d", handoff.closed ? 1 : 0);
}

///////////////////////////////////////////////////////////////////////////////
// Thread

class Returner : public Worker
{
public:
    explicit Returner(int value) : value_(value) {}

protected:
    virtual void work() { setReturnValue(value_); }

private:
    int value_;
};

DRV_CASE(threads_thread_lifecycle)
{
    Returner worker(17);

    outf("before run: running = %d, terminated = %d",
        worker.isRunning() ? 1 : 0, worker.isTerminated());

    worker.run();
    int returned = worker.waitFor();

    outf("waitFor = %s", intToStr(static_cast<INT64>(returned)).c_str());
    outf("getReturnValue = %s",
        intToStr(static_cast<INT64>(worker.getReturnValue())).c_str());
    outf("after join: running = %d", worker.isRunning() ? 1 : 0);
    outf("execute() ran = %d", worker.ran() ? 1 : 0);

    // Waiting again is not an error and does not change the answer: the
    // handle is cleared by the first wait and the return value is kept.
    outf("waitFor again = %s",
        intToStr(static_cast<INT64>(worker.waitFor())).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// Scheduling

// setPolicy and setPriority are the only part of the thread API whose answer
// is a pure function of its argument: both clamp out-of-range values, and
// what they clamp to is the library's decision rather than the kernel's.
// Nothing here asks the kernel for anything -- an unprivileged container will
// refuse SCHED_RR, and the library discards that failure -- so the case reads
// back what the object stored. That is the part a rewrite can silently get
// wrong, and until now the only part of requirement 9 with no scored input at
// all.
class Idler : public Worker
{
protected:
    virtual void work()
    {
        // nothing: this case is about what the thread object remembers, not
        // about what the thread does.
    }
};

DRV_CASE(threads_thread_scheduling_is_clamped)
{
    Idler worker;

    outf("fresh: policy = %d, priority = %d",
        worker.getPolicy(), worker.getPriority());

    worker.setPolicy(THREAD_POL_RR);
    outf("setPolicy(RR) -> %d", worker.getPolicy());
    worker.setPolicy(THREAD_POL_FIFO);
    outf("setPolicy(FIFO) -> %d", worker.getPolicy());
    worker.setPolicy(12345);
    outf("setPolicy(12345) -> %d", worker.getPolicy());

    worker.setPriority(THREAD_PRI_HIGH);
    outf("setPriority(HIGH) -> %d", worker.getPriority());
    worker.setPriority(THREAD_PRI_MAX);
    outf("setPriority(MAX) -> %d", worker.getPriority());
    worker.setPriority(THREAD_PRI_MAX + 1);
    outf("setPriority(MAX+1) -> %d", worker.getPriority());
    worker.setPriority(-1);
    outf("setPriority(-1) -> %d", worker.getPriority());

    // Both settings survive a run. The thread start path re-applies them to
    // the live thread and ignores whatever the kernel says about it, so a
    // container that refuses the policy and one that grants it print the same
    // two numbers here.
    worker.setPolicy(THREAD_POL_RR);
    worker.setPriority(THREAD_PRI_HIGH);
    worker.run();
    worker.waitFor();
    outf("after running: policy = %d, priority = %d, ran = %d",
        worker.getPolicy(), worker.getPriority(), worker.ran() ? 1 : 0);
}

// Sleeps in a loop until its owner asks it to stop. ThreadImpl::sleep watches
// the terminate flag between chunks, so terminate() is what ends this; the
// case never prints how long any of it took.
class Sleeper : public Worker
{
public:
    explicit Sleeper(Semaphore& roundDone) :
        roundDone_(&roundDone),
        rounds_(0)
    {
        // nothing
    }

    long rounds() const { return rounds_; }

protected:
    virtual void work()
    {
        while (!isTerminated())
        {
            sleep(0.05);
            rounds_ += 1;
            roundDone_->increase();
        }
        setReturnValue(7);
    }

private:
    Semaphore *roundDone_;
    long rounds_;
};

DRV_CASE(threads_thread_terminate_ends_sleep)
{
    // The first round is waited for, not slept for. This case used to sleep a
    // flat 0.1 s and then assert that at least one 0.05 s round had happened,
    // which is an assertion about the scheduler: on a loaded machine the
    // thread can still be between run() and its first sleep() when the owner
    // wakes up, and the line then prints 0 for a library that is working.
    // Waiting on the semaphore the round itself posts says the same thing
    // about the same library and says it under any load.
    Semaphore roundDone(0);
    Sleeper worker(roundDone);
    worker.run();
    roundDone.wait();
    worker.terminate();

    int returned = worker.waitFor();
    outf("terminated flag = %d", worker.isTerminated());
    outf("waitFor = %s", intToStr(static_cast<INT64>(returned)).c_str());
    outf("thread completed at least one round = %d",
        worker.rounds() > 0 ? 1 : 0);
}

// interruptSleep cuts a sleep short without asking the thread to stop. It is
// racy by construction -- ThreadImpl::sleep clears the flag on entry, so an
// interrupt delivered before the sleep starts is swallowed -- so the case
// waits until the thread is certainly executing, then retries until one lands.
//
// The first version of this case polled isRunning() instead of waiting on a
// semaphore, and read false on its first iteration because the thread had not
// reached execute() yet. It then reported a sleep "cut short" that had never
// started, and terminate() beat the thread to the flag, so it also reported a
// wake that was not early. Both answers were the scheduler's, not the
// library's, which is the one thing a fixture may never be.
class LongSleeper : public Worker
{
public:
    explicit LongSleeper(Semaphore& started) :
        started_(&started),
        wokeEarly_(false)
    {
        // nothing
    }

    bool wokeEarly() const { return wokeEarly_; }

protected:
    virtual void work()
    {
        started_->increase();
        sleep(30.0);
        wokeEarly_ = !isTerminated();
    }

private:
    Semaphore *started_;
    bool wokeEarly_;
};

DRV_CASE(threads_thread_interrupt_sleep)
{
    Semaphore started(0);
    LongSleeper worker(started);
    worker.run();
    started.wait();

    // Bounded: 200 attempts at 50 ms is ten seconds, a third of the sleep the
    // thread asked for. The library sleeps in half-second chunks and looks at
    // the flag between them, so the first interrupt that lands inside the
    // sleep takes effect within one chunk.
    for (int i = 0; i < 200 && worker.isRunning(); i++)
    {
        worker.interruptSleep();
        sleepSeconds(0.05, false);
    }

    bool stillRunning = worker.isRunning();
    worker.terminate();
    worker.waitFor();

    outf("sleep was cut short = %d", stillRunning ? 0 : 1);
    outf("woke without being terminated = %d", worker.wokeEarly() ? 1 : 0);
}

///////////////////////////////////////////////////////////////////////////////
// ThreadPool

// A task is a callable taking the thread that runs it. Written as a functor
// rather than a binder for the same reason as everything else in this driver:
// one file is compiled against every tree, and the tree decides what the
// library's callable type is, not this file.
class Task
{
public:
    Task(Mutex& mutex, long& done) : mutex_(&mutex), done_(&done) {}

    void operator()(Thread& thread) const
    {
        (void)thread;
        AutoLocker locker(*mutex_);
        *done_ += 1;
    }

private:
    Mutex *mutex_;
    long *done_;
};

// Everything the pool touches, kept alive past the end of the case.
//
// The case never stops or destroys its pool, and the reason is in the library
// rather than here. ThreadPool::stop() takes the pool's own mutex and holds it
// across ThreadList::waitForAllThreads. An idle worker is parked in
// Condition::wait() on that same mutex: notifyAll() wakes it, and it then
// blocks re-acquiring the mutex stop() is still holding. It never returns from
// takeTask, never looks at isTerminated(), and never removes itself from the
// list stop() is waiting to see empty. With stop()'s default of
// TIMEOUT_INFINITE that is a deadlock rather than a slow path, and
// ~ThreadPool calls stop() too -- so a pool on the stack hangs at the closing
// brace. Diagnosing that is not this driver's business; recording what the
// library does is, and what it does here is run every task it is given.
//
// So the pool is reached through a namespace-scope pointer and outlives the
// case, with its three workers parked in Condition::wait() until the process
// exits. The pointer matters for more than the hang: the behaviour suite is
// also what the inherited ownership-clean-under-sanitizers gate runs, and a
// fixture reachable from a global is still-reachable, which LeakSanitizer does
// not report. The same fixture left behind by a function would be lost. The
// mutex and the counter live in here too, so that no worker can be unlocking a
// mutex on main's dead stack frame.
struct PoolFixture
{
    ThreadPool pool;
    Mutex mutex;
    long done;

    PoolFixture() : done(0) {}
};

PoolFixture *g_poolFixture = 0;

DRV_CASE(threads_thread_pool_runs_every_task)
{
    const int TASKS = 30;

    if (g_poolFixture == 0)
        g_poolFixture = new PoolFixture;
    ThreadPool& pool = g_poolFixture->pool;

    outf("before start: running = %d", pool.isRunning() ? 1 : 0);
    pool.start(3);
    outf("after start: running = %d", pool.isRunning() ? 1 : 0);

    for (int i = 0; i < TASKS; i++)
        pool.addTask(Task(g_poolFixture->mutex, g_poolFixture->done));

    // Wait for the work rather than for a duration: bounded at ~20 s, which is
    // far past what thirty trivial tasks need on any machine, and the number
    // of iterations is never printed.
    long done = 0;
    for (int i = 0; i < 400; i++)
    {
        {
            AutoLocker locker(g_poolFixture->mutex);
            done = g_poolFixture->done;
        }
        if (done >= TASKS)
            break;
        sleepSeconds(0.05, false);
    }

    outf("tasks submitted = %s", intToStr(static_cast<INT64>(TASKS)).c_str());
    outf("tasks completed = %s", intToStr(static_cast<INT64>(done)).c_str());
    outf("pool still running afterwards = %d", pool.isRunning() ? 1 : 0);
}

///////////////////////////////////////////////////////////////////////////////
// Singleton

int g_singletonInits = 0;

class OnlyOne
{
public:
    OnlyOne() : serial_(++g_singletonInits) {}

    int serial() const { return serial_; }

private:
    int serial_;
};

// Asks for the instance from several threads at once, which is the only
// arrangement under which "exactly once" says anything.
class Asker : public Worker
{
public:
    Asker(Semaphore& gate, int& serial) : gate_(gate), serial_(serial) {}

protected:
    virtual void work()
    {
        gate_.wait();
        serial_ = Singleton<OnlyOne>::instance().serial();
    }

private:
    Semaphore& gate_;
    int& serial_;
};

DRV_CASE(threads_singleton_initialises_once)
{
    const int THREADS = 4;

    Semaphore gate(0);
    int serials[THREADS];
    for (int i = 0; i < THREADS; i++)
        serials[i] = -1;

    Asker a(gate, serials[0]);
    Asker b(gate, serials[1]);
    Asker c(gate, serials[2]);
    Asker d(gate, serials[3]);
    Worker *workers[THREADS];
    workers[0] = &a; workers[1] = &b; workers[2] = &c; workers[3] = &d;

    for (int i = 0; i < THREADS; i++)
        workers[i]->run();
    for (int i = 0; i < THREADS; i++)
        gate.increase();
    for (int i = 0; i < THREADS; i++)
        workers[i]->waitFor();

    bool allAgree = true;
    for (int i = 0; i < THREADS; i++)
        if (serials[i] != 1)
            allAgree = false;

    outf("constructions = %s",
        intToStr(static_cast<INT64>(g_singletonInits)).c_str());
    outf("every thread saw the first instance = %d", allAgree ? 1 : 0);
    outf("main sees the same one = %d",
        Singleton<OnlyOne>::instance().serial() == 1 ? 1 : 0);
}

///////////////////////////////////////////////////////////////////////////////
// The tick clock
//
// getCurTicks exists to be subtracted by getTickDiff and read as a number of
// milliseconds. Nothing else in the suite would notice if that unit changed,
// and a unit is exactly the kind of promise a rewrite of the clock can break
// without breaking a single compile.
//
// The classification below is monotone in load in the only direction that
// matters: a slower machine sleeps longer, so it can only push the reading
// further from zero, and the millisecond band is wide enough (a fifth of a
// second asked for, anything from a tenth of a second to a minute accepted)
// that no machine reaches the next unit up. A microsecond clock would read
// about 200 000 here and a nanosecond clock about 200 000 000, both outside
// the band; a second clock would read 0.

const char *classifyElapsed(UINT64 elapsed)
{
    if (elapsed >= 100 && elapsed <= 60000)
        return "milliseconds";
    if (elapsed < 100)
        return "coarser-than-milliseconds";
    return "finer-than-milliseconds";
}

DRV_CASE(threads_tick_unit)
{
    UINT64 before = getCurTicks();
    sleepSeconds(0.25, false);
    UINT64 after = getCurTicks();

    UINT64 elapsed = getTickDiff(before, after);
    outf("slept 0.25s; getTickDiff reads in %s", classifyElapsed(elapsed));

    // The clock does not run backwards over a sleep, and a difference taken
    // against itself is zero. Neither is a reading.
    outf("the clock advanced = %d", after >= before ? 1 : 0);
    outf("difference with itself = %s",
        intToStr(static_cast<INT64>(getTickDiff(after, after))).c_str());
}


///////////////////////////////////////////////////////////////////////////////
// AtomicInt, under contention

// classes_atomic_int already pins what the seven operations answer, one call
// at a time, on one thread: that `getAndAdd` returns the value from before the
// addition and `addAndGet` the value after it. What it cannot see is whether
// they are indivisible, which is the whole of requirement 3 and the one thing
// a plain `int` behind a lock would also get right if it were asked one call
// at a time.
//
// So: four threads, ten thousand increments each. The final value is forty
// thousand on any implementation where the operation is atomic, and on an
// implementation where it is not it is forty thousand only by luck -- a
// read-modify-write of a plain int loses updates under this much overlap on
// every machine this runs on. The number printed is the count, not a timing,
// and it is the same number on a fast machine and a slow one.
//
// The returned values are checked too, and by a property rather than by a
// transcript: each thread sums what getAndAdd handed back, and the four sums
// together must be the sum of 0..39999 if and only if every operation saw a
// distinct predecessor. A lost update shows up as a repeated return value and
// the total falls short. The arithmetic is exact in an INT64.

const int CONTENDERS = 4;
const int PER_CONTENDER = 10000;

class Adder : public Worker
{
public:
    Adder(AtomicInt& counter, Semaphore& start) :
        counter_(&counter),
        start_(&start),
        sumOfReturns_(0)
    {
        // nothing
    }

    INT64 sumOfReturns() const { return sumOfReturns_; }

protected:
    virtual void work()
    {
        // Every contender waits for the same gun, so the increments overlap
        // rather than running one thread after another.
        start_->wait();
        for (int i = 0; i < PER_CONTENDER; i++)
            sumOfReturns_ += static_cast<INT64>(counter_->getAndAdd(1));
    }

private:
    AtomicInt *counter_;
    Semaphore *start_;
    INT64 sumOfReturns_;
};

DRV_CASE(threads_atomic_contention)
{
    AtomicInt counter;
    Semaphore start(0);

    Adder *adders[CONTENDERS];
    Worker *workers[CONTENDERS];
    for (int i = 0; i < CONTENDERS; i++)
    {
        adders[i] = new Adder(counter, start);
        workers[i] = adders[i];
    }

    for (int i = 0; i < CONTENDERS; i++)
        workers[i]->run();
    for (int i = 0; i < CONTENDERS; i++)
        start.increase();
    for (int i = 0; i < CONTENDERS; i++)
        workers[i]->waitFor();

    const INT64 total = static_cast<INT64>(CONTENDERS) * PER_CONTENDER;
    INT64 sumOfReturns = 0;
    for (int i = 0; i < CONTENDERS; i++)
        sumOfReturns += adders[i]->sumOfReturns();

    outf("increments = %s", intToStr(total).c_str());
    outf("final value is the count = %d",
        static_cast<INT64>(counter.get()) == total ? 1 : 0);
    outf("every increment saw a distinct predecessor = %d",
        sumOfReturns == total * (total - 1) / 2 ? 1 : 0);

    for (int i = 0; i < CONTENDERS; i++)
        delete adders[i];
}

} // namespace
