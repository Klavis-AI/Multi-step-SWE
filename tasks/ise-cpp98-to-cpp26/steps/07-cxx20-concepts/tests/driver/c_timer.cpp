///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for ise_timer.h: Timer and TimerQueue.
//
// The timer example cannot cover any of this. Its three timers are scheduled
// against Timestamp::now(), which adds tv_usec -- microseconds -- to a
// millisecond count, so the framework's idea of "now" runs a thousand times
// too fast inside each wall second and falls back by about 999 seconds at
// every rollover. Measured: three runs of examples/timer over 130 seconds each
// fired their +3s, +5s and every-1s callbacks exactly never, and then all
// three at once when SIGTERM arrived (NOTES-authoring.md B18). When a callback
// runs is therefore a function of where in a wall second the process started,
// not of anything the program was asked to do.
//
// TimerQueue itself is a different matter: every expiry decision it makes is a
// comparison against the Timestamp it is handed. Passing fixed Timestamps
// instead of Timestamp::now() takes the clock out of the loop entirely, which
// is the only way an expectation may be taken from this file.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace ise;
using drv::out;
using drv::outf;

namespace {

// An arbitrary fixed instant, in the milliseconds Timestamp is documented to
// hold. Every expiration below is an offset from it, so the whole file reads
// as one timeline and none of it touches the clock.
const Timestamp::TimeVal T0 = 1234567890000LL;

Timestamp at(Timestamp::TimeVal offsetMs)
{
    return Timestamp(T0 + offsetMs);
}

// Callbacks append to this rather than printing, so a case can show the order
// the queue chose in one line and say nothing at all when nothing fired.
std::vector<std::string> g_fired;

std::string firedList()
{
    std::string result;
    for (size_t i = 0; i < g_fired.size(); i++)
    {
        if (i) result += ",";
        result += g_fired[i];
    }
    return result.empty() ? std::string("-") : result;
}

void note(const char *tag)
{
    g_fired.push_back(tag);
}

// `boost::bind(note, "a")` is how every registration below used to name its
// callback, and it worked only for as long as the tree included boost/bind.hpp
// on the way to ise.h. Step 3 moves that include inside the Windows branch --
// the point of that milestone is that the sources this environment compiles
// stop naming boost's callables -- and this driver, which is one file compiled
// against all eleven trees, cannot depend on a header the tree it is compiled
// against has stopped pulling in. A functor says the same thing in both
// dialects and depends on nothing outside this file, for the same reason
// CancellerRef below is a functor rather than boost::ref.
struct Note
{
    explicit Note(const char *tag) : tag_(tag) {}
    void operator()() const { note(tag_); }

    const char *tag_;
};

void reset()
{
    g_fired.clear();
}

// cancelTimer called from inside a callback is the interesting path: the timer
// being cancelled may be the one running, which the queue has already lifted
// out of its maps, or another one that is still in them.
struct Canceller
{
    Canceller(TimerQueue& queue, const char *tag) :
        queue_(queue), tag_(tag), target_(0) {}

    void operator()()
    {
        note(tag_);
        queue_.cancelTimer(target_);
    }

    TimerQueue& queue_;
    const char *tag_;
    TimerId target_;
};

// `boost::ref` is how the four registrations below were written, and it is
// a callback only boost::function knows how to call: a
// boost::reference_wrapper has no operator(), so the same line stops
// compiling the moment TimerCallback becomes std::function. std::ref would
// say it for the steps built at C++11 and does not exist at C++98, and this
// driver is one file for all of them. A forwarding functor is understood
// everywhere and calls the same object -- by pointer, so that the target_
// each caller assigns *after* registering the callback is the one the
// callback sees.
struct CancellerRef
{
    explicit CancellerRef(Canceller& target) : target_(&target) {}
    void operator()() const { (*target_)(); }

    Canceller *target_;
};

//-----------------------------------------------------------------------------

Timer* addAt(TimerQueue& queue, Timestamp::TimeVal offsetMs, INT64 interval,
    const TimerCallback& callback)
{
    Timer *timer = new Timer(at(offsetMs), interval, callback);
    queue.addTimer(timer);
    return timer;
}

std::string nearest(TimerQueue& queue)
{
    Timestamp expiration;
    if (!queue.getNearestExpiration(expiration))
        return "none";
    return intToStr(expiration.epochMilliseconds() - T0);
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// Timer

DRV_CASE(timer_object_fields)
{
    // repeat_ is `interval > 0.0`, so only a strictly positive interval
    // repeats; zero and negative both give a one-shot.
    static const INT64 INTERVALS[] = { 1000, 1, 0, -1, -1000 };
    for (int i = 0; i < DRV_COUNT(INTERVALS); i++)
    {
        Timer timer(at(500), INTERVALS[i], TimerCallback());
        outf("interval=%s repeat=%d expiration=%s",
            intToStr(INTERVALS[i]).c_str(), (timer.repeat() ? 1 : 0),
            intToStr(timer.expiration().epochMilliseconds() - T0).c_str());
    }
}

DRV_CASE(timer_restart)
{
    // restart() is where a repeating timer's schedule comes from, and it is
    // measured from the instant the queue got round to it rather than from the
    // tick it missed: a timer due at +1000 with a 1000 ms interval, processed
    // late at +9000, is next due at +10000 and not at +2000. Ten missed ticks
    // collapse into one.
    Timer repeating(at(1000), 1000, TimerCallback());
    repeating.restart(at(9000));
    outf("repeating: next=%s",
        intToStr(repeating.expiration().epochMilliseconds() - T0).c_str());

    // A one-shot restarts to the epoch rather than to anything on the timeline.
    Timer oneShot(at(1000), 0, TimerCallback());
    oneShot.restart(at(9000));
    outf("one-shot: next=%s (absolute %s)",
        intToStr(oneShot.expiration().epochMilliseconds() - T0).c_str(),
        intToStr(oneShot.expiration().epochMilliseconds()).c_str());
}

DRV_CASE(timer_ids_are_distinct_and_ascending)
{
    // Timer ids come from one process-wide SeqNumberAlloc, so their absolute
    // values depend on how many timers the framework itself has made. The
    // step between them does not.
    Timer a(at(0), 0, TimerCallback());
    Timer b(at(0), 0, TimerCallback());
    Timer c(at(0), 0, TimerCallback());
    outf("distinct=%d ascending=%d step=%s,%s",
        (a.timerId() != b.timerId() && b.timerId() != c.timerId()) ? 1 : 0,
        (a.timerId() < b.timerId() && b.timerId() < c.timerId()) ? 1 : 0,
        intToStr((INT64)(b.timerId() - a.timerId())).c_str(),
        intToStr((INT64)(c.timerId() - b.timerId())).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// TimerQueue

DRV_CASE(timer_queue_nearest)
{
    TimerQueue queue;
    outf("empty -> %s", nearest(queue).c_str());

    addAt(queue, 3000, 0, Note("c"));
    outf("one -> %s", nearest(queue).c_str());

    addAt(queue, 1000, 0, Note("a"));
    addAt(queue, 2000, 0, Note("b"));
    outf("three, out of order -> %s", nearest(queue).c_str());

    // Adding something later than the head does not move it.
    addAt(queue, 9000, 0, Note("d"));
    outf("after a later one -> %s", nearest(queue).c_str());
}

DRV_CASE(timer_queue_expiry_boundary)
{
    // processExpiredTimers takes everything up to and including `now`: the
    // bound it builds pairs `now` with the largest representable pointer, so a
    // timer whose expiration is exactly `now` sorts below it and fires.
    static const Timestamp::TimeVal NOWS[] = { 999, 1000, 1001, 5000 };
    for (int i = 0; i < DRV_COUNT(NOWS); i++)
    {
        reset();
        TimerQueue queue;
        addAt(queue, 1000, 0, Note("due1000"));
        addAt(queue, 2000, 0, Note("due2000"));
        queue.processExpiredTimers(at(NOWS[i]));
        outf("now=+%s fired=[%s] nearest=%s",
            intToStr(NOWS[i]).c_str(), firedList().c_str(),
            nearest(queue).c_str());
    }
}

DRV_CASE(timer_queue_fires_in_time_order)
{
    reset();
    TimerQueue queue;
    // Registered in one order, due in another.
    addAt(queue, 4000, 0, Note("fourth"));
    addAt(queue, 1000, 0, Note("first"));
    addAt(queue, 3000, 0, Note("third"));
    addAt(queue, 2000, 0, Note("second"));

    queue.processExpiredTimers(at(10000));
    outf("fired=[%s] nearest=%s", firedList().c_str(), nearest(queue).c_str());

    // Nothing is left, and processing again is a no-op rather than a repeat.
    reset();
    queue.processExpiredTimers(at(20000));
    outf("again fired=[%s]", firedList().c_str());
}

DRV_CASE(timer_queue_same_expiration)
{
    // Two timers due on the same millisecond. The set is keyed on
    // (Timestamp, Timer*), so they coexist and both fire; which of the two
    // goes first follows from the addresses the allocator handed out, so only
    // the count and the sorted set of tags are printed.
    reset();
    TimerQueue queue;
    addAt(queue, 1000, 0, Note("x"));
    addAt(queue, 1000, 0, Note("y"));
    addAt(queue, 1000, 0, Note("z"));
    queue.processExpiredTimers(at(1000));

    std::vector<std::string> sorted = g_fired;
    std::sort(sorted.begin(), sorted.end());
    std::string joined;
    for (size_t i = 0; i < sorted.size(); i++)
    {
        if (i) joined += ",";
        joined += sorted[i];
    }
    outf("count=%d sorted=[%s] nearest=%s",
        (int)g_fired.size(), joined.c_str(), nearest(queue).c_str());
}

DRV_CASE(timer_queue_repeating)
{
    reset();
    TimerQueue queue;
    addAt(queue, 1000, 1000, Note("tick"));

    // Each pass is one turn of an event loop that happened to wake at that
    // instant. A repeating timer fires at most once per pass however far
    // behind it is, and its next expiration is measured from the pass.
    static const Timestamp::TimeVal NOWS[] = { 500, 1000, 1500, 5000, 5001 };
    for (int i = 0; i < DRV_COUNT(NOWS); i++)
    {
        reset();
        queue.processExpiredTimers(at(NOWS[i]));
        outf("now=+%s fired=[%s] nearest=%s",
            intToStr(NOWS[i]).c_str(), firedList().c_str(),
            nearest(queue).c_str());
    }
}

DRV_CASE(timer_queue_cancel_before_expiry)
{
    reset();
    TimerQueue queue;
    Timer *first = addAt(queue, 1000, 0, Note("first"));
    Timer *second = addAt(queue, 2000, 0, Note("second"));
    TimerId firstId = first->timerId();

    queue.cancelTimer(firstId);
    outf("after cancel nearest=%s", nearest(queue).c_str());

    // Cancelling the same id twice, and cancelling one that never existed,
    // are both silent no-ops outside a callback.
    queue.cancelTimer(firstId);
    queue.cancelTimer(second->timerId() + 10000);
    outf("after repeat cancels nearest=%s", nearest(queue).c_str());

    queue.processExpiredTimers(at(9000));
    outf("fired=[%s] nearest=%s", firedList().c_str(), nearest(queue).c_str());
}

DRV_CASE(timer_queue_cancel_repeating)
{
    reset();
    TimerQueue queue;
    Timer *tick = addAt(queue, 1000, 1000, Note("tick"));
    TimerId id = tick->timerId();

    queue.processExpiredTimers(at(1000));
    outf("first pass fired=[%s] nearest=%s",
        firedList().c_str(), nearest(queue).c_str());

    reset();
    queue.cancelTimer(id);
    outf("after cancel nearest=%s", nearest(queue).c_str());

    queue.processExpiredTimers(at(9000));
    outf("second pass fired=[%s]", firedList().c_str());
}

DRV_CASE(timer_queue_cancel_from_callback)
{
    // A callback that cancels a timer which has not fired yet. That one is
    // still in the queue's maps, so the ordinary erase path runs.
    reset();
    TimerQueue queue;
    Canceller canceller(queue, "canceller");
    addAt(queue, 1000, 0, CancellerRef(canceller));
    Timer *victim = addAt(queue, 5000, 0, Note("victim"));
    canceller.target_ = victim->timerId();

    queue.processExpiredTimers(at(1000));
    outf("pass 1 fired=[%s] nearest=%s",
        firedList().c_str(), nearest(queue).c_str());

    reset();
    queue.processExpiredTimers(at(9000));
    outf("pass 2 fired=[%s] nearest=%s",
        firedList().c_str(), nearest(queue).c_str());
}

DRV_CASE(timer_queue_cancel_expiring_sibling)
{
    // Both timers are due on this pass, so the victim has already been lifted
    // out of the maps when the canceller runs. cancelTimer cannot find it and
    // records the id in cancelingTimers_ instead -- which only suppresses a
    // repeat, so a one-shot sibling still runs.
    reset();
    TimerQueue queue;
    Canceller canceller(queue, "canceller");
    addAt(queue, 1000, 0, CancellerRef(canceller));
    Timer *victim = addAt(queue, 1001, 0, Note("victim"));
    canceller.target_ = victim->timerId();

    queue.processExpiredTimers(at(2000));
    outf("one-shot sibling: fired=[%s] nearest=%s",
        firedList().c_str(), nearest(queue).c_str());

    // The same shape with a repeating victim: there the record does bite, and
    // the timer is dropped instead of rescheduled.
    reset();
    TimerQueue repeating;
    Canceller canceller2(repeating, "canceller");
    addAt(repeating, 1000, 0, CancellerRef(canceller2));
    Timer *ticker = addAt(repeating, 1001, 1000, Note("ticker"));
    canceller2.target_ = ticker->timerId();

    repeating.processExpiredTimers(at(2000));
    outf("repeating sibling: fired=[%s] nearest=%s",
        firedList().c_str(), nearest(repeating).c_str());
}

DRV_CASE(timer_queue_self_cancel_from_callback)
{
    // A repeating timer that cancels itself. It is out of the maps while its
    // callback runs, so the id lands in cancelingTimers_ and the repeat branch
    // drops it -- the documented way for a timer to stop itself, and the one
    // examples/timer uses after ten ticks.
    reset();
    TimerQueue queue;
    Canceller canceller(queue, "self");
    Timer *timer = addAt(queue, 1000, 1000, CancellerRef(canceller));
    canceller.target_ = timer->timerId();

    queue.processExpiredTimers(at(1000));
    outf("pass 1 fired=[%s] nearest=%s",
        firedList().c_str(), nearest(queue).c_str());

    reset();
    queue.processExpiredTimers(at(9000));
    outf("pass 2 fired=[%s] nearest=%s",
        firedList().c_str(), nearest(queue).c_str());
}

DRV_CASE(timer_queue_destructor_drops_pending)
{
    // The queue owns its timers. Nothing observable is left behind, so what
    // this case asserts is that a queue with work still in it can be destroyed
    // at all -- run under the sanitizers it also asserts the timers were freed
    // exactly once.
    reset();
    {
        TimerQueue queue;
        for (int i = 0; i < 8; i++)
            addAt(queue, 1000 * (i + 1), (i % 2) ? 500 : 0,
                Note("pending"));
        queue.processExpiredTimers(at(2500));
        outf("before destruction fired=[%s] nearest=%s",
            firedList().c_str(), nearest(queue).c_str());
    }
    outf("destroyed, fired total=%d", (int)g_fired.size());
}
