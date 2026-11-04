///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for ise_exceptions.h and the message table in
// ise_err_msgs.h.
//
// The other case files reach exceptions only as the thing a bad call throws,
// and they read the text with getErrorMessage(). What is pinned here is the
// hierarchy itself: which handler catches what, what makeLogStr() formats, and
// the fact that ise::Exception derives from std::exception *privately*, so a
// catch(std::exception&) handler does not see one.
//
// Nothing here prints __FILE__ or __LINE__ of a throw site. Source positions
// move whenever the source moves, which would make a correct migration fail.
// Where a case must exercise the file/line path it supplies a fixed pair of
// its own.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <algorithm>
#include <exception>
#include <string>

using namespace ise;
using drv::out;
using drv::outf;
using drv::esc;

DRV_CASE(exceptions_base)
{
    Exception base;
    outf("base getErrorMessage=[%s]", esc(base.getErrorMessage()).c_str());
    outf("base what=[%s]", esc(string(base.what())).c_str());
    outf("base makeLogStr=[%s]", esc(base.makeLogStr()).c_str());

    // what() caches into a mutable member, so a second call must still be
    // valid and equal to the first.
    string first = base.what();
    string second = base.what();
    outf("what stable across calls: %d", (first == second ? 1 : 0));
}

DRV_CASE(exceptions_simple)
{
    SimpleException empty;
    outf("default: msg=[%s] log=[%s]",
        esc(empty.getErrorMessage()).c_str(), esc(empty.makeLogStr()).c_str());

    SimpleException msgOnly("something went wrong");
    outf("message only: msg=[%s] log=[%s]",
        esc(msgOnly.getErrorMessage()).c_str(),
        esc(msgOnly.makeLogStr()).c_str());

    // A fixed, invented source position: the real __FILE__/__LINE__ of this
    // line would change whenever the file above it changed.
    SimpleException located("disk on fire", "src/fixed.cpp", 42);
    outf("with position: msg=[%s] log=[%s]",
        esc(located.getErrorMessage()).c_str(),
        esc(located.makeLogStr()).c_str());

    // The suffix is appended only when the name is non-empty *and* the line is
    // not negative, so each half of that condition gets its own case.
    SimpleException noFile("no file", NULL, 42);
    outf("line but no file: log=[%s]", esc(noFile.makeLogStr()).c_str());

    SimpleException emptyFile("empty file", "", 42);
    outf("line but empty file: log=[%s]", esc(emptyFile.makeLogStr()).c_str());

    SimpleException noLine("no line", "src/fixed.cpp");
    outf("file but default line: log=[%s]", esc(noLine.makeLogStr()).c_str());

    SimpleException lineZero("line zero", "src/fixed.cpp", 0);
    outf("file and line 0: log=[%s]", esc(lineZero.makeLogStr()).c_str());

    SimpleException lineNegative("line -2", "src/fixed.cpp", -2);
    outf("file and line -2: log=[%s]", esc(lineNegative.makeLogStr()).c_str());

    SimpleException nullMsg(NULL, "src/fixed.cpp", 7);
    outf("null message: msg=[%s] log=[%s]",
        esc(nullMsg.getErrorMessage()).c_str(),
        esc(nullMsg.makeLogStr()).c_str());

    SimpleException emptyMsg("", "src/fixed.cpp", 7);
    outf("empty message: log=[%s]", esc(emptyMsg.makeLogStr()).c_str());

    // The setter is spelt with three s's in the interface. That spelling is
    // part of the interface until a step says otherwise.
    SimpleException mutable_("before");
    mutable_.setErrorMesssage("after");
    outf("after setErrorMesssage: msg=[%s] what=[%s]",
        esc(mutable_.getErrorMessage()).c_str(),
        esc(string(mutable_.what())).c_str());

    // A message with bytes a formatter could mangle.
    SimpleException awkward("100% done: \"a\" \t<b>\n");
    outf("awkward message: log=[%s]", esc(awkward.makeLogStr()).c_str());
}

DRV_CASE(exceptions_file)
{
    // With an explicit message the error code contributes nothing to the text.
    FileException explicit_("data.bin", 2, "could not read it");
    outf("explicit message: msg=[%s] log=[%s]",
        esc(explicit_.getErrorMessage()).c_str(),
        esc(explicit_.makeLogStr()).c_str());

    // Without one the message comes from the C library's strerror, so it is
    // scored for being present and for differing between codes rather than by
    // its exact words.
    FileException noEnt("missing.bin", 2);
    FileException accessDenied("locked.bin", 13);
    outf("derived from errno: non-empty=%d distinct=%d",
        (noEnt.getErrorMessage().empty() ? 0 : 1),
        (noEnt.getErrorMessage() != accessDenied.getErrorMessage() ? 1 : 0));

    // A FileException carries no source position, so its log string is the
    // message with the prefix and nothing else.
    outf("log has no position suffix: %d",
        (explicit_.makeLogStr() ==
            "ERROR: could not read it" ? 1 : 0));

    FileException defaulted;
    outf("default: msg=[%s] log=[%s]",
        esc(defaulted.getErrorMessage()).c_str(),
        esc(defaulted.makeLogStr()).c_str());
}

DRV_CASE(exceptions_derived_types)
{
    // Every leaf type carries its message through the same SimpleException
    // slot, so the interesting part is that each one still reports as itself.
    MemoryException memory("out of it");
    StreamException stream("stream trouble");
    ThreadException thread("thread trouble");
    SocketException socket("socket trouble");
    DbException db("db trouble");
    DataAlgoException algo("algo trouble");

    outf("memory: [%s]", esc(memory.getErrorMessage()).c_str());
    outf("stream: [%s]", esc(stream.getErrorMessage()).c_str());
    outf("thread: [%s]", esc(thread.getErrorMessage()).c_str());
    outf("socket: [%s]", esc(socket.getErrorMessage()).c_str());
    outf("db: [%s]", esc(db.getErrorMessage()).c_str());
    outf("algo: [%s]", esc(algo.getErrorMessage()).c_str());

    MemoryException defaulted;
    outf("default-constructed leaf: msg=[%s] log=[%s]",
        esc(defaulted.getErrorMessage()).c_str(),
        esc(defaulted.makeLogStr()).c_str());

    // makeLogStr is virtual and SimpleException overrides it, so a leaf
    // reached through a base reference must use the override.
    Exception& asBase = stream;
    outf("virtual through base ref: log=[%s] what=[%s]",
        esc(asBase.makeLogStr()).c_str(),
        esc(string(asBase.what())).c_str());
}

DRV_CASE(exceptions_handler_matching)
{
    // A leaf is caught by a handler for its own type before one for its base.
    try
    {
        throw SocketException("leaf first");
    }
    catch (SocketException& e)
    {
        outf("socket handler: [%s]", esc(e.getErrorMessage()).c_str());
    }
    catch (SimpleException&)
    {
        out("simple handler (wrong)");
    }

    // ... and by a handler for its base when no leaf handler is present.
    try
    {
        throw DbException("caught as simple");
    }
    catch (SimpleException& e)
    {
        outf("simple handler: [%s]", esc(e.getErrorMessage()).c_str());
    }

    try
    {
        throw ThreadException("caught as base");
    }
    catch (Exception& e)
    {
        outf("base handler: [%s] log=[%s]",
            esc(e.getErrorMessage()).c_str(), esc(e.makeLogStr()).c_str());
    }

    // ise::Exception inherits std::exception privately, so the conversion to
    // the standard base is inaccessible and a catch(std::exception&) handler
    // does not match. This is the behaviour of the code as it stands; a step
    // that makes the inheritance public would change it, which is exactly why
    // it is pinned.
    try
    {
        throw StreamException("not a std::exception to a handler");
    }
    catch (std::exception&)
    {
        out("caught by std::exception handler");
    }
    catch (Exception& e)
    {
        outf("fell through to ise::Exception handler: [%s]",
            esc(e.getErrorMessage()).c_str());
    }

    // The same question asked of a plain ise::Exception.
    try
    {
        throw Exception();
    }
    catch (std::exception&)
    {
        out("base caught by std::exception handler");
    }
    catch (Exception&)
    {
        out("base fell through to ise::Exception handler");
    }

    // A sibling type must not be caught by another leaf's handler.
    try
    {
        throw MemoryException("sibling");
    }
    catch (SocketException&)
    {
        out("caught by sibling handler (wrong)");
    }
    catch (MemoryException& e)
    {
        outf("caught by own handler: [%s]", esc(e.getErrorMessage()).c_str());
    }

    // An exception thrown through a function boundary and rethrown keeps its
    // type and its message.
    try
    {
        try
        {
            throw SocketException("rethrown");
        }
        catch (Exception&)
        {
            throw;
        }
    }
    catch (SocketException& e)
    {
        outf("rethrow preserved the type: [%s]",
            esc(e.getErrorMessage()).c_str());
    }
}

DRV_CASE(exceptions_throwing_helpers)
{
    // Each helper must throw the type it is named for. Catching the sibling
    // first would let a helper that threw the wrong type pass unnoticed, so
    // each case catches something it must *not* be before what it must be.
    try
    {
        iseThrowMemoryException();
    }
    catch (StreamException&)
    {
        out("memory helper threw a StreamException (wrong)");
    }
    catch (MemoryException& e)
    {
        outf("memory helper: [%s]", esc(e.getErrorMessage()).c_str());
    }

    try
    {
        iseThrowStreamException("stream helper message");
    }
    catch (MemoryException&)
    {
        out("stream helper threw a MemoryException (wrong)");
    }
    catch (StreamException& e)
    {
        outf("stream helper: [%s]", esc(e.getErrorMessage()).c_str());
    }

    try
    {
        iseThrowThreadException("thread helper message");
    }
    catch (SocketException&)
    {
        out("thread helper threw a SocketException (wrong)");
    }
    catch (ThreadException& e)
    {
        outf("thread helper: [%s]", esc(e.getErrorMessage()).c_str());
    }

    try
    {
        iseThrowSocketException("socket helper message");
    }
    catch (ThreadException&)
    {
        out("socket helper threw a ThreadException (wrong)");
    }
    catch (SocketException& e)
    {
        outf("socket helper: [%s]", esc(e.getErrorMessage()).c_str());
    }

    try
    {
        iseThrowDbException("db helper message");
    }
    catch (DataAlgoException&)
    {
        out("db helper threw a DataAlgoException (wrong)");
    }
    catch (DbException& e)
    {
        outf("db helper: [%s]", esc(e.getErrorMessage()).c_str());
    }

    try
    {
        iseThrowFileException("some.txt", 13, "explicit file message");
    }
    catch (StreamException&)
    {
        out("file helper threw a StreamException (wrong)");
    }
    catch (FileException& e)
    {
        outf("file helper: [%s]", esc(e.getErrorMessage()).c_str());
    }

    // iseThrowException without a position is the plain form.
    try
    {
        iseThrowException("plain message");
    }
    catch (SimpleException& e)
    {
        outf("plain helper: msg=[%s] log=[%s]",
            esc(e.getErrorMessage()).c_str(), esc(e.makeLogStr()).c_str());
    }

    // ISE_THROW_EXCEPTION splices in __FILE__ and __LINE__, whose values move
    // with the source. The shape of what it produces is scored; the position
    // itself is not.
    try
    {
        ISE_THROW_EXCEPTION("macro message");
    }
    catch (SimpleException& e)
    {
        string log = e.makeLogStr();
        bool prefixed = log.compare(0, 7, "ERROR: ") == 0;
        bool hasMessage = log.find("macro message") != string::npos;
        bool bracketed = !log.empty() && log[log.size() - 1] == ')' &&
            log.find(" (") != string::npos;
        bool namesThisFile = log.find("c_exceptions.cpp:") != string::npos;
        outf("macro: prefixed=%d message=%d bracketed=%d names-this-file=%d",
            (prefixed ? 1 : 0), (hasMessage ? 1 : 0), (bracketed ? 1 : 0),
            (namesThisFile ? 1 : 0));
        outf("macro getErrorMessage is the bare message: %d",
            (e.getErrorMessage() == "macro message" ? 1 : 0));
    }
}

DRV_CASE(exceptions_message_table)
{
    // The message table is a wall of const char* const at namespace scope. A
    // migration to constexpr string_view or to an enum would be free to change
    // the storage and must not change a single byte of the text, because these
    // strings reach users through logs and through getErrorMessage().
    struct Entry { const char *name; const char *value; };
    static const Entry ENTRIES[] =
    {
        { "SEM_OUT_OF_MEMORY", SEM_OUT_OF_MEMORY },
        { "SEM_FEATURE_NOT_SUPPORTED", SEM_FEATURE_NOT_SUPPORTED },
        { "SEM_INDEX_ERROR", SEM_INDEX_ERROR },
        { "SEM_INVALID_DATETIME_STR", SEM_INVALID_DATETIME_STR },
        { "SEM_SEM_INIT_ERROR", SEM_SEM_INIT_ERROR },
        { "SEM_COND_INIT_ERROR", SEM_COND_INIT_ERROR },
        { "SEM_FAIL_TO_CREATE_MUTEX", SEM_FAIL_TO_CREATE_MUTEX },
        { "SEM_CANNOT_CREATE_FILE", SEM_CANNOT_CREATE_FILE },
        { "SEM_CANNOT_OPEN_FILE", SEM_CANNOT_OPEN_FILE },
        { "SEM_SET_FILE_STREAM_SIZE_ERR", SEM_SET_FILE_STREAM_SIZE_ERR },
        { "SEM_LIST_CAPACITY_ERROR", SEM_LIST_CAPACITY_ERROR },
        { "SEM_LIST_COUNT_ERROR", SEM_LIST_COUNT_ERROR },
        { "SEM_LIST_INDEX_ERROR", SEM_LIST_INDEX_ERROR },
        { "SEM_SORTED_LIST_ERROR", SEM_SORTED_LIST_ERROR },
        { "SEM_PROPLIST_NAME_ERROR", SEM_PROPLIST_NAME_ERROR },
        { "SEM_STRINGS_NAME_ERROR", SEM_STRINGS_NAME_ERROR },
        { "SEM_DUPLICATE_STRING", SEM_DUPLICATE_STRING },
        { "SEM_STREAM_READ_ERROR", SEM_STREAM_READ_ERROR },
        { "SEM_STREAM_WRITE_ERROR", SEM_STREAM_WRITE_ERROR },
        { "SEM_PACKET_UNPACK_ERROR", SEM_PACKET_UNPACK_ERROR },
        { "SEM_PACKET_PACK_ERROR", SEM_PACKET_PACK_ERROR },
        { "SEM_UNSAFE_VALUE_IN_PACKET", SEM_UNSAFE_VALUE_IN_PACKET },
        { "SEM_THREAD_RUN_ONCE", SEM_THREAD_RUN_ONCE },
        { "SEM_INVALID_XML_FILE_FORMAT", SEM_INVALID_XML_FILE_FORMAT },
        { "SEM_BUSINESS_OBJ_EXPECTED", SEM_BUSINESS_OBJ_EXPECTED },
        { "SEM_ALREADY_RUNNING", SEM_ALREADY_RUNNING },
        { "SEM_INIT_DAEMON_ERROR", SEM_INIT_DAEMON_ERROR },
        { "SEM_CREATE_PIPE_ERROR", SEM_CREATE_PIPE_ERROR },
        { "SEM_CREATE_EPOLL_ERROR", SEM_CREATE_EPOLL_ERROR },
        { "SEM_EPOLL_WAIT_ERROR", SEM_EPOLL_WAIT_ERROR },
        { "SEM_INVALID_OP_FOR_IOCP", SEM_INVALID_OP_FOR_IOCP },
        { "SEM_EVENT_LOOP_NOT_SPECIFIED", SEM_EVENT_LOOP_NOT_SPECIFIED }
    };

    for (int i = 0; i < DRV_COUNT(ENTRIES); i++)
        outf("%s = [%s]", ENTRIES[i].name, esc(ENTRIES[i].value).c_str());

    // The entries that carry printf placeholders are the ones a migration to a
    // type-safe formatter would have to rewrite, so their placeholders are
    // pinned by count as well as by text. Each string is named before it is
    // walked: a begin() and an end() taken from two separate temporaries are
    // iterators into two separate objects.
    string openFile(SEM_CANNOT_OPEN_FILE);
    outf("SEM_CANNOT_OPEN_FILE placeholders: %d",
        static_cast<int>(std::count(openFile.begin(), openFile.end(), '%')));

    string indexError(SEM_LIST_INDEX_ERROR);
    outf("SEM_LIST_INDEX_ERROR placeholders: %d",
        static_cast<int>(std::count(indexError.begin(), indexError.end(), '%')));

    // formatString over the two-placeholder entry is how the file exceptions
    // actually build their text.
    outf("formatted open failure: [%s]",
        esc(formatString(SEM_CANNOT_OPEN_FILE, "a.txt", "No such file"))
            .c_str());
    outf("formatted index error: [%s]",
        esc(formatString(SEM_LIST_INDEX_ERROR, -1)).c_str());
}

DRV_CASE(exceptions_assert_failure)
{
    // ISE_ASSERT is live in every build of this project -- ise_options.h
    // defines ISE_DEBUG unconditionally -- and the library calls it in
    // fourteen places. Only the passing branch is ever taken there: an
    // assertion firing on the baseline would be a bug. That left
    // internalAssert() the one function in ise/main no scored input reached,
    // and its contract -- log the failure, then throw -- is exactly what a
    // migration to <cassert> or to contracts would quietly change.
    //
    // So the condition asserted here is the driver's own. Coaxing one of the
    // library's own invariants into failing would be testing the library in a
    // state its contract says it is never in.
    ISE_ASSERT(1 == 1);
    out("assert on a true condition returns normally");

    // Not a literal: a literal-false condition is what a compiler is most
    // likely to fold, and the point is to reach the function.
    const int zero = 0;

    try
    {
        ISE_ASSERT(zero == 1);
        out("no exception");
    }
    catch (SimpleException& e)
    {
        // getErrorMessage() only. makeLogStr() appends the __FILE__ and
        // __LINE__ of the expansion site, which is this line of this file, so
        // recording it would turn every edit above it into a false difference.
        outf("caught SimpleException: [%s]", esc(e.getErrorMessage()).c_str());
    }

    // The thrown type also has to be catchable as the base, since that is how
    // every other caller in the tree handles it.
    try
    {
        ISE_ASSERT(zero > 0);
        out("no exception");
    }
    catch (Exception& e)
    {
        outf("caught as base Exception: [%s]", esc(e.getErrorMessage()).c_str());
    }

    // Exception derives from std::exception privately (see exceptions_base),
    // so this handler must not see it -- pinned here too, because an assertion
    // that started escaping a catch(std::exception&) would change the
    // behaviour of every program that links the library.
    try
    {
        ISE_ASSERT(zero != 0);
        out("no exception");
    }
    catch (std::exception&)
    {
        out("caught as std::exception");
    }
    catch (Exception&)
    {
        out("not caught as std::exception; caught as ise::Exception");
    }
}
