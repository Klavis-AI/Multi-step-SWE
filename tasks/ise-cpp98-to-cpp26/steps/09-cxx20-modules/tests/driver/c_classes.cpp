///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for ise_classes.h: Buffer, DateTime, Timestamp,
// SeqNumberAlloc, AtomicInt/AtomicInt64 and ObjectList.
//
// Nothing here prints a value the framework does not promise. Buffer bytes are
// only read back where the case wrote them or asked for initZero; the two
// now() clocks are checked for shape and ordering, never for a value.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <string>

using namespace ise;
using drv::out;
using drv::outf;
using drv::esc;

namespace {

// A fixed instant, so every date case has one unambiguous answer:
// 2009-02-13 23:31:30 UTC.
const time_t FIXED_EPOCH = 1234567890;

//-----------------------------------------------------------------------------
// Stack conditioning for encodeDateTime.
//
// encodeDateTime declares `struct tm tm;` and assigns six of its fields, so
// mktime reads a tm_isdst the library never wrote -- whatever byte pattern the
// previous call to that stack depth left behind. Its answer therefore moves
// between two runs of the same binary (NOTES-authoring.md B11), and nothing
// downstream of it can be scored as it stands.
//
// These two helpers put a chosen byte there instead, which makes the answer a
// function of the inputs again without touching the library. Filling with 0x00
// gives tm_isdst == 0, "no daylight saving", which is both the answer a
// fixed encodeDateTime produces and the answer TZ=UTC deserves; filling with
// 0x01 gives a positive tm_isdst, a request for a daylight offset the zone
// does not have, which glibc answers an hour earlier.
//
// The dependency this creates is on the compiler and flags the *verifier* uses
// to build the driver, which are fixed by the task, not on anything the agent
// being scored can change. It is checked in both of them.

void ISE_NOINLINE dirtyFrame(unsigned char fill)
{
    // Larger than any frame encodeDateTime can claim, and volatile so the
    // stores survive: the whole purpose of this function is its side effect on
    // memory that is dead by the language's reckoning.
    volatile unsigned char pad[512];
    for (int i = 0; i < static_cast<int>(sizeof(pad)); i++)
        pad[i] = fill;
}

DateTime ISE_NOINLINE encodeWith(unsigned char fill, const int *fields)
{
    dirtyFrame(fill);
    DateTime result;
    result.encodeDateTime(fields[0], fields[1], fields[2],
        fields[3], fields[4], fields[5]);
    return result;
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// Buffer

DRV_CASE(classes_buffer_lifecycle)
{
    Buffer b;
    outf("empty: size=%d pos=%d data-null=%d c_str=[%s]",
        b.getSize(), b.getPosition(), (b.data() == NULL ? 1 : 0), b.c_str());

    b.assign("hello", 5);
    outf("assign: size=%d pos=%d c_str=[%s] esc=[%s]",
        b.getSize(), b.getPosition(), b.c_str(), esc(b.data(), b.getSize()).c_str());

    // Shrinking keeps the surviving prefix; c_str() re-terminates at the new
    // size, so the dropped bytes must not reappear.
    b.setSize(3);
    outf("shrink3: size=%d c_str=[%s]", b.getSize(), b.c_str());

    // Growing with initZero fills only the new tail.
    b.setSize(6, true);
    outf("grow6zero: size=%d esc=[%s]", b.getSize(), esc(b.data(), b.getSize()).c_str());

    b.clear();
    outf("clear: size=%d pos=%d data-null=%d c_str=[%s]",
        b.getSize(), b.getPosition(), (b.data() == NULL ? 1 : 0), b.c_str());
}

DRV_CASE(classes_buffer_position_clamp)
{
    // setPosition does not fail; it clamps. Both ends, and re-clamping when the
    // buffer later shrinks under a position that was legal when it was set.
    Buffer b("0123456789", 10);
    static const int POSITIONS[] = { -1000, -1, 0, 5, 10, 11, 1000 };
    for (int i = 0; i < DRV_COUNT(POSITIONS); i++)
    {
        b.setPosition(POSITIONS[i]);
        outf("set %d -> %d", POSITIONS[i], b.getPosition());
    }

    b.setPosition(9);
    b.setSize(4);
    outf("after shrink to 4, pos = %d", b.getPosition());

    b.setSize(0);
    outf("after setSize(0), pos = %d size = %d", b.getPosition(), b.getSize());
}

DRV_CASE(classes_buffer_copy_and_assign)
{
    Buffer a("abcdef", 6);
    a.setPosition(2);

    // The copy constructor copies the bytes. Whether it also copies the
    // position is the sort of thing a migration could change by accident, so
    // it is recorded rather than assumed.
    Buffer b(a);
    outf("copy-ctor: size=%d pos=%d c_str=[%s]", b.getSize(), b.getPosition(), b.c_str());

    Buffer c;
    c = a;
    outf("copy-assign: size=%d pos=%d c_str=[%s]", c.getSize(), c.getPosition(), c.c_str());

    // Self-assignment must not free the bytes out from under itself.
    c = c;
    outf("self-assign: size=%d c_str=[%s]", c.getSize(), c.c_str());

    // Mutating the copy must not touch the original.
    b[0] = 'Z';
    outf("independent: a=[%s] b=[%s]", a.c_str(), b.c_str());

    Buffer sized(4);
    outf("explicit-size: size=%d pos=%d", sized.getSize(), sized.getPosition());
}

DRV_CASE(classes_buffer_zero_and_negative_sizes)
{
    // Every entry point that takes a count, given zero and negative counts.
    Buffer b;
    b.assign("xyz", 0);
    outf("assign 0: size=%d data-null=%d", b.getSize(), (b.data() == NULL ? 1 : 0));

    b.assign("xyz", -4);
    outf("assign -4: size=%d data-null=%d", b.getSize(), (b.data() == NULL ? 1 : 0));

    b.assign(NULL, 0);
    outf("assign NULL/0: size=%d", b.getSize());

    b.setSize(-7);
    outf("setSize -7: size=%d", b.getSize());

    b.assign("abc", 3);
    b.ensureSize(2);
    outf("ensureSize smaller: size=%d", b.getSize());
    b.ensureSize(8);
    outf("ensureSize larger: size=%d", b.getSize());
}

DRV_CASE(classes_buffer_binary_roundtrip)
{
    // A buffer is a byte container, not a string: embedded NULs and high bytes
    // must survive assign/copy, and getSize() -- not strlen -- must be what
    // reports the length.
    char raw[8];
    raw[0] = 'a'; raw[1] = '\0'; raw[2] = 'b'; raw[3] = '\n';
    raw[4] = static_cast<char>(0xFF); raw[5] = static_cast<char>(0x80);
    raw[6] = '\0'; raw[7] = 'z';

    Buffer b(raw, 8);
    outf("size=%d esc=[%s]", b.getSize(), esc(b.data(), b.getSize()).c_str());
    outf("c_str stops at first NUL: strlen=%d", static_cast<int>(strlen(b.c_str())));

    Buffer copy = b;
    outf("copy esc=[%s]", esc(copy.data(), copy.getSize()).c_str());

    outf("index read: [0]=%s [4]=%s",
        esc(&b[0], 1).c_str(), esc(&b[4], 1).c_str());
}

DRV_CASE(classes_buffer_file_roundtrip)
{
    std::string path = drv::tmpPath("buffer.bin");

    Buffer b("payload\0with-nul", 16);
    outf("save: %d", (b.saveToFile(path) ? 1 : 0));
    outf("size on disk: %d", static_cast<int>(getFileSize(path)));

    Buffer r;
    outf("load: %d", (r.loadFromFile(path) ? 1 : 0));
    outf("loaded size=%d esc=[%s]", r.getSize(), esc(r.data(), r.getSize()).c_str());
    outf("position after load: %d", r.getPosition());

    Buffer missing("junk", 4);
    bool loaded = missing.loadFromFile(drv::tmpPath("nope.bin"));
    outf("load missing: %d size=%d", (loaded ? 1 : 0), missing.getSize());

    Buffer empty;
    outf("save empty: %d", (empty.saveToFile(drv::tmpPath("empty.bin")) ? 1 : 0));
    outf("empty size on disk: %d",
        static_cast<int>(getFileSize(drv::tmpPath("empty.bin"))));
}

///////////////////////////////////////////////////////////////////////////////
// DateTime

DRV_CASE(classes_datetime_decode_fixed)
{
    DateTime dt(FIXED_EPOCH);
    int year, month, day, hour, minute, second, weekDay, yearDay;
    dt.decodeDateTime(&year, &month, &day, &hour, &minute, &second, &weekDay, &yearDay);

    outf("epoch=%d", static_cast<int>(dt.epochTime()));
    outf("%04d-%02d-%02d %02d:%02d:%02d wday=%d yday=%d",
        year, month, day, hour, minute, second, weekDay, yearDay);
    outf("date=[%s]", dt.toDateString().c_str());
    outf("date/=[%s]", dt.toDateString("/").c_str());
    outf("date-empty-sep=[%s]", dt.toDateString("").c_str());
    outf("datetime=[%s]", dt.toDateTimeString().c_str());
    outf("datetime-T=[%s]", dt.toDateTimeString("-", "T", ":").c_str());

    // Every out-parameter is optional; asking for none must not crash.
    dt.decodeDateTime(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    out("all-NULL decode survived");
}

// The instants encodeDateTime is asked about, shared by the case below and by
// the defect case that shows what it does with them: the epoch itself, a leap
// day, the last second of a year, the 32-bit time_t rollover, and a spread of
// out-of-range fields that mktime normalises rather than rejecting.
static const int ENCODE_CASES[][6] =
{
    { 1970,  1,  1,  0,  0,  0 },
    { 2000,  2, 29, 12, 30, 45 },
    { 1999, 12, 31, 23, 59, 59 },
    { 2009,  2, 13, 23, 31, 30 },
    { 2038,  1, 19,  3, 14,  7 },
    { 2100,  3,  1,  0,  0,  1 },
    { 2024,  2, 29, 23, 59, 59 },
    { 2011, 13,  1,  0,  0,  0 },
    { 2011,  0,  1,  0,  0,  0 },
    { 2011,  1, 32,  0,  0,  0 },
    { 2011,  1,  0,  0,  0,  0 },
    { 2011,  2, 30,  0,  0,  0 },
    { 2011,  1,  1, 25,  0,  0 },
    { 2011,  1,  1,  0, 90,  0 },
    { 2011,  1,  1,  0,  0, 90 },
    { 2011,  1,  1, -1, -1, -1 }
};

DRV_UNSCORED_CASE(classes_datetime_encode_indeterminate,
    "encodeDateTime reads an indeterminate tm_isdst; see NOTES-authoring.md B11")
{
    // NOT SCORED. encodeDateTime declares
    //
    //     struct tm tm;
    //
    // and then assigns six of its fields, leaving tm_isdst -- and tm_gmtoff
    // and tm_zone -- holding whatever the last call to use that stack left
    // there. mktime reads tm_isdst. Under TZ=UTC a request for daylight time
    // that the zone does not have makes glibc fall back to assuming a
    // one-hour offset, so the same call answers t on one run of the same
    // binary and t-3600 on the next, depending on the byte pattern the
    // framework's startup happened to leave at that stack depth.
    //
    // Valgrind names both ends of it:
    //
    //   Conditional jump or move depends on uninitialised value(s)
    //      at isdst_differ (mktime.c:191)
    //      by __mktime_internal (mktime.c:426)
    //      by ise::DateTime::encodeDateTime(...) (ise_classes.cpp:311)
    //    Uninitialised value was created by a stack allocation
    //      at ise::DateTime::encodeDateTime(...) (ise_classes.cpp:301)
    //
    // So every value below is one of two admissible answers, and no
    // expectation can be taken from any of them -- not the epoch, not the
    // round trip, not the normalisation of an out-of-range field, because a
    // month of 0 that lands on 2010-12-01 00:00:00 in one run lands on
    // 2010-11-30 23:00:00 in the next. The whole of encodeDateTime is
    // therefore outside the scored set, which is the strongest statement this
    // suite can make about it. Everything DateTime does from a time_t goes
    // through localtime_r and is scored normally.
    for (int i = 0; i < DRV_COUNT(ENCODE_CASES); i++)
    {
        DateTime dt;
        dt.encodeDateTime(ENCODE_CASES[i][0], ENCODE_CASES[i][1], ENCODE_CASES[i][2],
            ENCODE_CASES[i][3], ENCODE_CASES[i][4], ENCODE_CASES[i][5]);
        outf("%04d-%02d-%02d %02d:%02d:%02d -> epoch=%s -> [%s]",
            ENCODE_CASES[i][0], ENCODE_CASES[i][1], ENCODE_CASES[i][2],
            ENCODE_CASES[i][3], ENCODE_CASES[i][4], ENCODE_CASES[i][5],
            intToStr(static_cast<INT64>(dt.epochTime())).c_str(),
            dt.toDateTimeString().c_str());
    }

    // Defaulted time-of-day.
    DateTime midnight;
    midnight.encodeDateTime(2011, 6, 15);
    outf("date-only -> [%s]", midnight.toDateTimeString().c_str());
}

DRV_CASE(classes_datetime_encode_table)
{
    // The same instants, asked with tm_isdst conditioned to 0 (see
    // encodeWith): the calendar arithmetic and the normalisation of every
    // out-of-range field, which is the part of encodeDateTime that is a
    // property of the library rather than of the stack. A leap day, the last
    // second of a year, the 32-bit time_t rollover, a month of 13 and a month
    // of 0, a day of 32 and a day of 0, February 30th, and negative fields.
    for (int i = 0; i < DRV_COUNT(ENCODE_CASES); i++)
    {
        DateTime dt = encodeWith(0x00, ENCODE_CASES[i]);
        outf("%04d-%02d-%02d %02d:%02d:%02d -> epoch=%s -> [%s]",
            ENCODE_CASES[i][0], ENCODE_CASES[i][1], ENCODE_CASES[i][2],
            ENCODE_CASES[i][3], ENCODE_CASES[i][4], ENCODE_CASES[i][5],
            intToStr(static_cast<INT64>(dt.epochTime())).c_str(),
            dt.toDateTimeString().c_str());
    }
}

DRV_CASE(classes_datetime_encode_is_a_function_of_its_inputs)
{
    // The one thing about encodeDateTime that the conditioning above cannot
    // paper over: whether the library's answer depends on anything but the
    // library's inputs. Asked by handing it two stacks that differ only in a
    // byte it never writes. Today it answers differently for all sixteen, so
    // this reads 0; a migration that gives tm_isdst a value -- any value, or
    // no struct tm at all -- makes it 1 and keeps it there.
    //
    // Sixteen inputs and not one, because an input whose two admissible
    // answers happened to coincide would otherwise report a clean run.
    int disagreements = 0;
    for (int i = 0; i < DRV_COUNT(ENCODE_CASES); i++)
    {
        if (encodeWith(0x00, ENCODE_CASES[i]).epochTime() !=
            encodeWith(0x01, ENCODE_CASES[i]).epochTime())
            disagreements++;
    }
    outf("inputs: %d", DRV_COUNT(ENCODE_CASES));
    outf("answer independent of untouched stack: %d",
        (disagreements == 0 ? 1 : 0));
}

DRV_CASE(classes_datetime_from_string)
{
    // Only a 19-character string is accepted; everything else throws. The
    // accepted form is parsed by fixed offsets, not by the separators, so a
    // string with the right length and the wrong punctuation still parses.
    //
    // What is recorded is the accept-or-throw decision and the message,
    // because that much is a length test and nothing else. The parsed value
    // is not recorded: operator=(const string&) finishes by calling
    // encodeDateTime, so the DateTime it leaves behind is one of two answers
    // (see classes_datetime_encode_indeterminate).
    static const char *INPUTS[] =
    {
        "2009-02-13 23:31:30",
        "2009/02/13T23:31:30",
        "2009x02x13x23x31x30",
        "0000-00-00 00:00:00",
        "9999-99-99 99:99:99",
        "2009-02-13 23:31:3",
        "2009-02-13 23:31:300",
        "",
        "                   ",
        "abcd-ef-gh ij:kl:mn"
    };

    for (int i = 0; i < DRV_COUNT(INPUTS); i++)
    {
        DateTime dt;
        try
        {
            dt = string(INPUTS[i]);
            outf("[%s] -> accepted", INPUTS[i]);
        }
        catch (Exception& e)
        {
            outf("[%s] -> throw: %s", INPUTS[i], e.getErrorMessage().c_str());
        }
    }

    // The string constructor takes the same path.
    try
    {
        DateTime ctor(string("2020-01-02 03:04:05"));
        out("ctor -> accepted");
    }
    catch (Exception& e)
    {
        outf("ctor -> throw: %s", e.getErrorMessage().c_str());
    }
}

DRV_CASE(classes_datetime_arithmetic_and_compare)
{
    DateTime a(FIXED_EPOCH);
    DateTime b(FIXED_EPOCH + 3600);

    outf("a+b epoch = %s", intToStr(static_cast<INT64>((a + b).epochTime())).c_str());
    outf("b-a epoch = %s", intToStr(static_cast<INT64>((b - a).epochTime())).c_str());
    outf("a+3600 = [%s]", (a + static_cast<time_t>(3600)).toDateTimeString().c_str());
    outf("a-3600 = [%s]", (a - static_cast<time_t>(3600)).toDateTimeString().c_str());

    outf("== %d  != %d  < %d  > %d  <= %d  >= %d",
        (a == b ? 1 : 0), (a != b ? 1 : 0), (a < b ? 1 : 0),
        (a > b ? 1 : 0), (a <= b ? 1 : 0), (a >= b ? 1 : 0));

    DateTime c = a;
    outf("self: == %d  <= %d  >= %d  < %d",
        (c == a ? 1 : 0), (c <= a ? 1 : 0), (c >= a ? 1 : 0), (c < a ? 1 : 0));

    // Assignment from a raw time_t and from a Timestamp.
    DateTime d;
    d = static_cast<time_t>(0);
    outf("from time_t 0 = [%s]", d.toDateTimeString().c_str());

    Timestamp ts(static_cast<Timestamp::TimeVal>(FIXED_EPOCH) * 1000 + 250);
    d = ts;
    outf("from Timestamp = [%s] (millis dropped)", d.toDateTimeString().c_str());

    // Negative time_t: before the epoch.
    DateTime before(static_cast<time_t>(-86400));
    outf("epoch-1day = [%s]", before.toDateTimeString().c_str());
}

DRV_CASE(classes_datetime_now_shape)
{
    // now() is the wall clock, so only its shape and its relation to itself
    // can be scored.
    DateTime t1 = DateTime::now();
    DateTime t2 = DateTime::now();

    outf("monotonic: %d", (t2 >= t1 ? 1 : 0));
    outf("after 2020: %d", (t1.epochTime() > 1577836800 ? 1 : 0));

    string s = t1.toDateTimeString();
    outf("length: %d", static_cast<int>(s.length()));
    outf("separators: %d %d %d %d",
        (s[4] == '-' ? 1 : 0), (s[7] == '-' ? 1 : 0),
        (s[10] == ' ' ? 1 : 0), (s[13] == ':' ? 1 : 0));

    string d = t1.toDateString();
    outf("date is the datetime prefix: %d",
        (s.compare(0, d.length(), d) == 0 ? 1 : 0));
}

///////////////////////////////////////////////////////////////////////////////
// Timestamp

DRV_CASE(classes_timestamp_fixed)
{
    Timestamp::TimeVal value =
        static_cast<Timestamp::TimeVal>(FIXED_EPOCH) * 1000 + 456;
    Timestamp ts(value);

    outf("millis = %s", intToStr(ts.epochMilliseconds()).c_str());
    outf("seconds = %s", intToStr(static_cast<INT64>(ts.epochTime())).c_str());
    outf("toString = [%s]", ts.toString().c_str());
    outf("toString custom = [%s]", ts.toString("/", "T", "-", ",").c_str());
    outf("toString empty seps = [%s]", ts.toString("", "", "", "").c_str());

    // setEpochTime resets the sub-second part rather than preserving it.
    Timestamp reset;
    reset.setEpochTime(FIXED_EPOCH);
    outf("setEpochTime millis = %s", intToStr(reset.epochMilliseconds()).c_str());
    outf("setEpochTime toString = [%s]", reset.toString().c_str());

    Timestamp zero;
    outf("default millis = %s toString = [%s]",
        intToStr(zero.epochMilliseconds()).c_str(), zero.toString().c_str());
}

DRV_CASE(classes_timestamp_arithmetic)
{
    Timestamp base(static_cast<Timestamp::TimeVal>(FIXED_EPOCH) * 1000);

    outf("+1500 = [%s]", (base + 1500).toString().c_str());
    outf("-1 = [%s]", (base - 1).toString().c_str());
    outf("diff = %s", intToStr((base + 2500) - base).c_str());
    outf("reverse diff = %s", intToStr(base - (base + 2500)).c_str());

    Timestamp t = base;
    t += 10000;
    outf("+= 10000 -> [%s]", t.toString().c_str());
    t -= 25000;
    outf("-= 25000 -> [%s]", t.toString().c_str());

    Timestamp a(1000), b(2000);
    outf("== %d != %d < %d > %d <= %d >= %d",
        (a == b ? 1 : 0), (a != b ? 1 : 0), (a < b ? 1 : 0),
        (a > b ? 1 : 0), (a <= b ? 1 : 0), (a >= b ? 1 : 0));

    // Assigning a DateTime takes the second, so the millisecond part is zero.
    Timestamp fromDt;
    fromDt = DateTime(FIXED_EPOCH);
    outf("from DateTime millis = %s", intToStr(fromDt.epochMilliseconds()).c_str());

    // A negative value is before the epoch; epochTime() truncates toward zero
    // rather than toward minus infinity, which is worth pinning.
    Timestamp negative(-1500);
    outf("negative: millis=%s seconds=%s",
        intToStr(negative.epochMilliseconds()).c_str(),
        intToStr(static_cast<INT64>(negative.epochTime())).c_str());
}

DRV_CASE(classes_timestamp_now_shape)
{
    Timestamp t1 = Timestamp::now();
    Timestamp t2;
    t2.update();

    // "t2 >= t1" is not here, tempting as it is. Timestamp::now() is
    //
    //     value_ = TimeVal(tv.tv_sec) * MILLISECS_PER_SECOND + tv.tv_usec;
    //
    // and tv_usec is microseconds, so the value climbs a thousand times too
    // fast inside a wall second and drops by about 999000 at every rollover
    // (NOTES-authoring.md B18, measured). Two samples taken a few hundred
    // nanoseconds apart are almost always in the same second and almost always
    // ordered -- almost being the problem. Nothing below turns the sub-second
    // part into an expectation, which also leaves a migration free to fix the
    // unit without failing a fixture that had pinned the defect.
    outf("update wrote a value: %d", (t2.epochMilliseconds() != 0 ? 1 : 0));
    outf("plausible millis: %d",
        (t1.epochMilliseconds() > 1577836800000LL ? 1 : 0));
    outf("seconds agree with millis: %d",
        (static_cast<INT64>(t1.epochTime()) == t1.epochMilliseconds() / 1000 ? 1 : 0));

    string s = t1.toString();
    outf("length: %d", static_cast<int>(s.length()));
    outf("microsec separator: %d", (s[19] == '.' ? 1 : 0));
}

///////////////////////////////////////////////////////////////////////////////
// SeqNumberAlloc

DRV_CASE(classes_seq_number_alloc)
{
    // One allocId() per statement: two in the same argument list would be
    // evaluated in whichever order the compiler chose.
    SeqNumberAlloc seq;
    for (int i = 0; i < 3; i++)
        outf("default: %s", intToStr(static_cast<INT64>(seq.allocId())).c_str());

    SeqNumberAlloc from100(100);
    for (int i = 0; i < 2; i++)
        outf("from 100: %s", intToStr(static_cast<INT64>(from100.allocId())).c_str());

    // Starting one below the unsigned 64-bit ceiling: the next allocation
    // wraps. Where it wraps to is a property of the counter's type.
    SeqNumberAlloc atMax(0xFFFFFFFFFFFFFFFFULL);
    UINT64 last = atMax.allocId();
    UINT64 wrapped = atMax.allocId();
    outf("at max: last-is-max=%d wrapped-to-zero=%d",
        (last == 0xFFFFFFFFFFFFFFFFULL ? 1 : 0), (wrapped == 0 ? 1 : 0));

    // 1000 allocations are consecutive with no gaps.
    SeqNumberAlloc run(7);
    bool consecutive = true;
    UINT64 expected = 7;
    for (int i = 0; i < 1000; i++)
    {
        if (run.allocId() != expected++) consecutive = false;
    }
    outf("1000 consecutive: %d, next = %s",
        (consecutive ? 1 : 0), intToStr(static_cast<INT64>(run.allocId())).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// AtomicInt / AtomicInt64

DRV_CASE(classes_atomic_int)
{
    AtomicInt a;
    outf("initial = %s", intToStr(static_cast<INT64>(a.get())).c_str());
    outf("increment = %s", intToStr(static_cast<INT64>(a.increment())).c_str());
    outf("increment = %s", intToStr(static_cast<INT64>(a.increment())).c_str());
    outf("decrement = %s", intToStr(static_cast<INT64>(a.decrement())).c_str());
    // One mutating call per statement throughout: the order in which a
    // compiler evaluates two arguments of the same call is unspecified, so a
    // line that printed both the return value and the new value would be
    // recording the compiler rather than the library.
    long got = a.getAndAdd(10);
    outf("getAndAdd 10 = %s", intToStr(static_cast<INT64>(got)).c_str());
    outf("  now %s", intToStr(static_cast<INT64>(a.get())).c_str());
    outf("addAndGet -4 = %s", intToStr(static_cast<INT64>(a.addAndGet(-4))).c_str());
    got = a.getAndSet(99);
    outf("getAndSet 99 = %s", intToStr(static_cast<INT64>(got)).c_str());
    outf("  now %s", intToStr(static_cast<INT64>(a.get())).c_str());
    outf("set 0 returns %s", intToStr(static_cast<INT64>(a.set(0))).c_str());

    // get() is implemented as a compare-and-swap against zero; a counter
    // sitting at zero must still read back as zero, and reading must not
    // disturb the value.
    outf("read zero twice: %s %s",
        intToStr(static_cast<INT64>(a.get())).c_str(),
        intToStr(static_cast<INT64>(a.get())).c_str());

    AtomicInt64 b;
    INT64 big = 4000000000LL;
    b.set(big);
    outf("64-bit beyond 32 bits: %s", intToStr(b.get()).c_str());
    outf("64-bit addAndGet: %s", intToStr(b.addAndGet(big)).c_str());
    b.set(-1);
    outf("64-bit negative: %s", intToStr(b.get()).c_str());
    outf("64-bit decrement from -1: %s", intToStr(b.decrement()).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// ObjectList

namespace {

int g_liveTracked = 0;
std::string g_destroyOrder;

class Tracked
{
public:
    explicit Tracked(const std::string& tag) : tag_(tag) { g_liveTracked++; }
    ~Tracked()
    {
        g_liveTracked--;
        if (!g_destroyOrder.empty()) g_destroyOrder += ",";
        g_destroyOrder += tag_;
    }
    const std::string& tag() const { return tag_; }
private:
    std::string tag_;
};

} // namespace

DRV_CASE(classes_object_list_owning)
{
    g_liveTracked = 0;
    g_destroyOrder.clear();

    {
        ObjectList<Tracked> list;
        Tracked *a = new Tracked("a");
        Tracked *b = new Tracked("b");
        Tracked *c = new Tracked("c");

        outf("add returns %d", list.add(a));
        outf("add returns %d", list.add(b));
        outf("add returns %d", list.add(c));
        outf("count=%d first=%s last=%s",
            list.getCount(), list.first()->tag().c_str(), list.last()->tag().c_str());
        outf("indexOf b = %d, exists c = %d, isEmpty = %d",
            list.indexOf(b), (list.exists(c) ? 1 : 0), (list.isEmpty() ? 1 : 0));
        outf("operator[] 1 = %s, getItem 2 = %s",
            list[1]->tag().c_str(), list.getItem(2)->tag().c_str());

        // remove() deletes, because the list owns its objects.
        int removed = list.remove(b);
        outf("remove b -> index %d, live now %d", removed, g_liveTracked);

        // extract() hands ownership back instead.
        Tracked *taken = list.extract(a);
        outf("extract a -> %s, live still %d, count %d",
            (taken ? taken->tag().c_str() : "NULL"), g_liveTracked, list.getCount());
        delete taken;

        list.insert(0, new Tracked("d"));
        outf("after insert: count=%d [0]=%s [1]=%s",
            list.getCount(), list[0]->tag().c_str(), list[1]->tag().c_str());

        g_destroyOrder.clear();
    }

    outf("destroyed on scope exit: [%s], live = %d",
        g_destroyOrder.c_str(), g_liveTracked);
}

DRV_CASE(classes_object_list_non_owning)
{
    g_liveTracked = 0;
    g_destroyOrder.clear();

    Tracked a("a"), b("b");
    {
        ObjectList<Tracked> list(false, false);
        list.add(&a);
        list.add(&b);
        list.remove(&a);
        outf("non-owning remove kept the object: live=%d count=%d",
            g_liveTracked, list.getCount());
    }
    outf("non-owning clear kept the objects: live=%d order=[%s]",
        g_liveTracked, g_destroyOrder.c_str());

    // setOwnsObjects flips the policy on a live list.
    {
        ObjectList<Tracked> list(false, false);
        list.add(new Tracked("x"));
        list.setOwnsObjects(true);
        list.clear();
        outf("after flipping to owning and clearing: live=%d order=[%s]",
            g_liveTracked, g_destroyOrder.c_str());
    }
}

DRV_CASE(classes_object_list_edges)
{
    g_liveTracked = 0;
    g_destroyOrder.clear();

    ObjectList<Tracked> list;

    // Every query against an empty list.
    outf("empty: count=%d isEmpty=%d first-null=%d last-null=%d",
        list.getCount(), (list.isEmpty() ? 1 : 0),
        (list.first() == NULL ? 1 : 0), (list.last() == NULL ? 1 : 0));

    Tracked *ghost = new Tracked("ghost");
    outf("indexOf absent = %d", list.indexOf(ghost));
    outf("exists absent = %d", (list.exists(ghost) ? 1 : 0));
    outf("remove absent = %d", list.remove(ghost));
    outf("extract absent = %d", (list.extract(ghost) == NULL ? 1 : 0));

    // Out-of-range del() is a no-op rather than an error.
    list.del(-1);
    list.del(0);
    list.del(100);
    out("out-of-range del survived");

    // extract(index) out of range returns NULL.
    outf("extract(-1) null = %d", (list.extract(-1) == NULL ? 1 : 0));
    outf("extract(0) null = %d", (list.extract(0) == NULL ? 1 : 0));

    list.add(ghost);
    // Adding the same pointer twice is allowed by default but not when
    // duplicates are refused; a list holding one pointer twice would delete it
    // twice, so which is the default matters.
    int dupIndex = list.add(ghost);
    outf("duplicate add returns %d, count = %d", dupIndex, list.getCount());
    list.extract(ghost);
    list.extract(ghost);
    outf("count after extracting both = %d, live = %d", list.getCount(), g_liveTracked);
    delete ghost;
    outf("final live = %d", g_liveTracked);
}
