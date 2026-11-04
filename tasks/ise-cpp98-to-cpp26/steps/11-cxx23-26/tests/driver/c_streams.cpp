///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for the stream classes in ise_classes.h:
// Stream, MemoryStream and FileStream.
//
// Streams hand back uninitialised memory in several places (a grown
// MemoryStream, a partial read), so every case here reads back only bytes it
// wrote, or bytes it can name the value of.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <string>

using namespace ise;
using drv::out;
using drv::outf;
using drv::esc;

namespace {

// The bytes of a stream from 0 to its size, leaving the position where it was.
std::string dumpStream(Stream& stream)
{
    INT64 saved = stream.getPosition();
    INT64 size = stream.getSize();
    std::string result;
    if (size > 0)
    {
        result.resize(static_cast<size_t>(size));
        stream.setPosition(0);
        int got = stream.read(&result[0], static_cast<int>(size));
        result.resize(got > 0 ? static_cast<size_t>(got) : 0);
    }
    stream.setPosition(saved);
    return drv::esc(result);
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// MemoryStream

DRV_CASE(streams_memory_basics)
{
    MemoryStream stream;
    outf("empty: size=%s pos=%s",
        intToStr(stream.getSize()).c_str(), intToStr(stream.getPosition()).c_str());

    outf("write 5 -> %d", stream.write("hello", 5));
    outf("after write: size=%s pos=%s",
        intToStr(stream.getSize()).c_str(), intToStr(stream.getPosition()).c_str());
    outf("contents: [%s]", dumpStream(stream).c_str());

    // Writing at the end appends; writing in the middle overwrites without
    // changing the size.
    outf("append 6 -> %d", stream.write(" world", 6));
    outf("size now %s contents [%s]",
        intToStr(stream.getSize()).c_str(), dumpStream(stream).c_str());

    stream.setPosition(0);
    outf("overwrite 5 -> %d", stream.write("HELLO", 5));
    outf("size still %s contents [%s]",
        intToStr(stream.getSize()).c_str(), dumpStream(stream).c_str());

    // A zero-length and a negative-length write.
    outf("write 0 -> %d", stream.write("x", 0));
    outf("write -1 -> %d", stream.write("x", -1));
    outf("size unchanged: %s", intToStr(stream.getSize()).c_str());

    stream.clear();
    outf("after clear: size=%s pos=%s",
        intToStr(stream.getSize()).c_str(), intToStr(stream.getPosition()).c_str());
}

DRV_CASE(streams_memory_read)
{
    MemoryStream stream;
    stream.write("0123456789", 10);

    char buffer[32];

    stream.setPosition(0);
    int got = stream.read(buffer, 4);
    outf("read 4 -> %d [%s] pos=%s",
        got, esc(buffer, got > 0 ? got : 0).c_str(),
        intToStr(stream.getPosition()).c_str());

    // A read that asks for more than remains returns what there is, rather
    // than failing.
    got = stream.read(buffer, 100);
    outf("read past end -> %d [%s] pos=%s",
        got, esc(buffer, got > 0 ? got : 0).c_str(),
        intToStr(stream.getPosition()).c_str());

    // A read at the end returns zero.
    got = stream.read(buffer, 4);
    outf("read at end -> %d", got);

    outf("read 0 -> %d", stream.read(buffer, 0));
    outf("read -1 -> %d", stream.read(buffer, -1));

    // readBuffer insists on the full count and throws when it cannot.
    stream.setPosition(0);
    try
    {
        stream.readBuffer(buffer, 4);
        outf("readBuffer 4 ok [%s]", esc(buffer, 4).c_str());
    }
    catch (Exception& e)
    {
        outf("readBuffer 4 throw: %s", e.getErrorMessage().c_str());
    }

    try
    {
        stream.readBuffer(buffer, 100);
        out("readBuffer 100 ok");
    }
    catch (Exception& e)
    {
        outf("readBuffer 100 throw: %s", e.getErrorMessage().c_str());
    }
}

DRV_CASE(streams_memory_seek)
{
    MemoryStream stream;
    stream.write("0123456789", 10);

    struct { INT64 offset; SEEK_ORIGIN origin; const char *name; } CASES[] =
    {
        { 0,    DRV_ENUM(SEEK_ORIGIN, SO_BEGINNING), "beginning+0" },
        { 5,    DRV_ENUM(SEEK_ORIGIN, SO_BEGINNING), "beginning+5" },
        { 10,   DRV_ENUM(SEEK_ORIGIN, SO_BEGINNING), "beginning+10" },
        { 11,   DRV_ENUM(SEEK_ORIGIN, SO_BEGINNING), "beginning+11" },
        { -1,   DRV_ENUM(SEEK_ORIGIN, SO_BEGINNING), "beginning-1" },
        { 1000, DRV_ENUM(SEEK_ORIGIN, SO_BEGINNING), "beginning+1000" },
        { 0,    DRV_ENUM(SEEK_ORIGIN, SO_END),       "end+0" },
        { -3,   DRV_ENUM(SEEK_ORIGIN, SO_END),       "end-3" },
        { 3,    DRV_ENUM(SEEK_ORIGIN, SO_END),       "end+3" },
        { -100, DRV_ENUM(SEEK_ORIGIN, SO_END),       "end-100" }
    };

    for (int i = 0; i < 10; i++)
    {
        INT64 result = stream.seek(CASES[i].offset, CASES[i].origin);
        outf("seek %s -> %s (pos %s)", CASES[i].name,
            intToStr(result).c_str(), intToStr(stream.getPosition()).c_str());
    }

    // SO_CURRENT is relative, so it has to be walked from a known point.
    stream.setPosition(4);
    outf("current+2 -> %s",
        intToStr(stream.seek(2, DRV_ENUM(SEEK_ORIGIN, SO_CURRENT))).c_str());
    outf("current-3 -> %s",
        intToStr(stream.seek(-3, DRV_ENUM(SEEK_ORIGIN, SO_CURRENT))).c_str());
    outf("current-100 -> %s",
        intToStr(stream.seek(-100, DRV_ENUM(SEEK_ORIGIN, SO_CURRENT))).c_str());
    outf("current+100 -> %s",
        intToStr(stream.seek(100, DRV_ENUM(SEEK_ORIGIN, SO_CURRENT))).c_str());
}

DRV_CASE(streams_memory_set_size)
{
    MemoryStream stream;
    stream.write("0123456789", 10);
    stream.setPosition(7);

    // Truncating below the position must move the position with it.
    stream.setSize(4);
    outf("truncate to 4: size=%s pos=%s contents=[%s]",
        intToStr(stream.getSize()).c_str(),
        intToStr(stream.getPosition()).c_str(), dumpStream(stream).c_str());

    // Growing leaves the new tail unspecified, so only the surviving prefix
    // and the reported size are checked.
    stream.setSize(9);
    outf("grow to 9: size=%s pos=%s", intToStr(stream.getSize()).c_str(),
        intToStr(stream.getPosition()).c_str());
    char prefix[4];
    stream.setPosition(0);
    stream.readBuffer(prefix, 4);
    outf("prefix survived: [%s]", esc(prefix, 4).c_str());

    stream.setSize(0);
    outf("setSize 0: size=%s pos=%s", intToStr(stream.getSize()).c_str(),
        intToStr(stream.getPosition()).c_str());

    stream.setSize(-5);
    outf("setSize -5: size=%s", intToStr(stream.getSize()).c_str());
}

DRV_CASE(streams_memory_growth)
{
    // The delta is rounded up to MIN_MEMORY_DELTA and to a power of two, and a
    // write larger than the delta still has to work in one call.
    static const int DELTAS[] = { 1, 16, 255, 256, 257, 1024, 4096 };
    for (int i = 0; i < DRV_COUNT(DELTAS); i++)
    {
        MemoryStream stream(DELTAS[i]);
        std::string payload(3000, 'x');
        int written = stream.write(payload.data(), 3000);
        stream.write("END", 3);
        outf("delta %d: wrote %d, size %s", DELTAS[i], written,
            intToStr(stream.getSize()).c_str());

        char tail[3];
        stream.setPosition(3000);
        stream.readBuffer(tail, 3);
        outf("  tail [%s]", esc(tail, 3).c_str());
    }

    // Many small writes must produce the same bytes as one large one.
    MemoryStream incremental;
    for (int i = 0; i < 2000; i++)
        incremental.write("ab", 2);
    outf("2000 small writes: size=%s", intToStr(incremental.getSize()).c_str());

    std::string readBack;
    readBack.resize(4000);
    incremental.setPosition(0);
    incremental.readBuffer(&readBack[0], 4000);
    bool same = true;
    for (int i = 0; i < 4000; i++)
        if (readBack[i] != (i % 2 == 0 ? 'a' : 'b')) same = false;
    outf("bytes as expected: %d", (same ? 1 : 0));
}

DRV_CASE(streams_memory_copy)
{
    MemoryStream source;
    source.write("payload", 7);
    source.setPosition(3);

    MemoryStream copyCtor(source);
    outf("copy-ctor: size=%s pos=%s contents=[%s]",
        intToStr(copyCtor.getSize()).c_str(),
        intToStr(copyCtor.getPosition()).c_str(), dumpStream(copyCtor).c_str());

    MemoryStream assigned;
    assigned.write("stale data here", 15);
    assigned = source;
    outf("assigned: size=%s contents=[%s]",
        intToStr(assigned.getSize()).c_str(), dumpStream(assigned).c_str());

    // Deep, not shared: writing to the copy must not change the source.
    copyCtor.setPosition(0);
    copyCtor.write("XXXXXXX", 7);
    outf("source unchanged: [%s]", dumpStream(source).c_str());

    // Self-assignment must not free the buffer it is about to read.
    assigned = assigned;
    outf("self-assign: size=%s contents=[%s]",
        intToStr(assigned.getSize()).c_str(), dumpStream(assigned).c_str());

    MemoryStream fromEmpty;
    MemoryStream emptyCopy(fromEmpty);
    outf("copy of empty: size=%s", intToStr(emptyCopy.getSize()).c_str());
}

DRV_CASE(streams_memory_files)
{
    std::string path = drv::tmpPath("memstream.bin");

    MemoryStream stream;
    stream.write("first\0second", 12);

    outf("saveToFile = %d", (stream.saveToFile(path) ? 1 : 0));
    outf("bytes on disk = %d", static_cast<int>(getFileSize(path)));

    MemoryStream loaded;
    loaded.write("stale", 5);
    outf("loadFromFile = %d", (loaded.loadFromFile(path) ? 1 : 0));
    outf("size=%s contents=[%s] pos=%s",
        intToStr(loaded.getSize()).c_str(), dumpStream(loaded).c_str(),
        intToStr(loaded.getPosition()).c_str());

    MemoryStream missing;
    missing.write("stale", 5);
    bool ok = missing.loadFromFile(drv::tmpPath("nope.bin"));
    outf("load missing = %d, size now %s", (ok ? 1 : 0),
        intToStr(missing.getSize()).c_str());

    // Stream to stream.
    MemoryStream target;
    stream.setPosition(0);
    outf("saveToStream = %d", (stream.saveToStream(target) ? 1 : 0));
    outf("target size=%s contents=[%s]",
        intToStr(target.getSize()).c_str(), dumpStream(target).c_str());

    MemoryStream fromStream;
    target.setPosition(0);
    outf("loadFromStream = %d", (fromStream.loadFromStream(target) ? 1 : 0));
    outf("contents=[%s]", dumpStream(fromStream).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// FileStream

DRV_CASE(streams_file_create_and_read)
{
    std::string path = drv::tmpPath("filestream.bin");

    {
        FileStream file;
        outf("before open: isOpen=%d", (file.isOpen() ? 1 : 0));
        outf("open FM_CREATE = %d", (file.open(path, FM_CREATE) ? 1 : 0));
        outf("after open: isOpen=%d name=[%s]",
            (file.isOpen() ? 1 : 0), file.getFileName().c_str());
        outf("write -> %d", file.write("abcdefghij", 10));
        outf("size=%s pos=%s",
            intToStr(file.getSize()).c_str(), intToStr(file.getPosition()).c_str());
    }

    outf("bytes on disk = %d", static_cast<int>(getFileSize(path)));

    {
        FileStream file(path, FM_OPEN_READ);
        outf("reopened: isOpen=%d size=%s",
            (file.isOpen() ? 1 : 0), intToStr(file.getSize()).c_str());

        char buffer[16];
        int got = file.read(buffer, 4);
        outf("read 4 -> %d [%s]", got, esc(buffer, got > 0 ? got : 0).c_str());

        outf("seek end-2 -> %s",
            intToStr(file.seek(-2, DRV_ENUM(SEEK_ORIGIN, SO_END))).c_str());
        got = file.read(buffer, 16);
        outf("read rest -> %d [%s]", got, esc(buffer, got > 0 ? got : 0).c_str());

        // Writing to a read-only handle fails rather than throwing.
        outf("write to read-only -> %d", file.write("x", 1));
    }

    // FM_CREATE on an existing file truncates it.
    {
        FileStream file(path, FM_CREATE);
        outf("recreate: size=%s", intToStr(file.getSize()).c_str());
        file.write("new", 3);
    }
    outf("after recreate, bytes on disk = %d", static_cast<int>(getFileSize(path)));
}

DRV_CASE(streams_file_open_failures)
{
    FileStream file;

    // A path that does not exist, a directory, and a path whose parent does
    // not exist. None may throw from open() itself.
    outf("open missing for read = %d",
        (file.open(drv::tmpPath("absent.bin"), FM_OPEN_READ) ? 1 : 0));
    outf("  isOpen = %d", (file.isOpen() ? 1 : 0));

    outf("open directory = %d", (file.open(drv::tmpDir(), FM_OPEN_READ) ? 1 : 0));
    outf("  isOpen = %d", (file.isOpen() ? 1 : 0));

    outf("create under missing parent = %d",
        (file.open(drv::tmpPath("no/such/dir/x.bin"), FM_CREATE) ? 1 : 0));

    // The exception out-parameter is filled in on failure.
    FileException error;
    bool opened = file.open(drv::tmpPath("absent.bin"), FM_OPEN_READ,
        DEFAULT_FILE_ACCESS_RIGHTS, &error);
    outf("with exception out-param: opened=%d message-empty=%d",
        (opened ? 1 : 0), (error.getErrorMessage().empty() ? 1 : 0));

    // Operating on a closed stream must not crash.
    FileStream closed;
    char buffer[4];
    outf("read from closed -> %d", closed.read(buffer, 4));
    outf("write to closed -> %d", closed.write("x", 1));
    outf("seek on closed -> %s",
        intToStr(closed.seek(0, DRV_ENUM(SEEK_ORIGIN, SO_BEGINNING))).c_str());
    outf("size of closed -> %s", intToStr(closed.getSize()).c_str());
    closed.close();
    out("double close survived");
}

DRV_CASE(streams_file_set_size_and_append)
{
    std::string path = drv::tmpPath("resize.bin");

    FileStream file(path, FM_CREATE);
    file.write("0123456789", 10);

    file.setSize(4);
    outf("truncate to 4: size=%s pos=%s",
        intToStr(file.getSize()).c_str(), intToStr(file.getPosition()).c_str());

    // Growing a file leaves a hole, which reads back as zeroes -- that part is
    // promised by the filesystem, so it can be scored.
    file.setSize(8);
    outf("grow to 8: size=%s", intToStr(file.getSize()).c_str());
    char buffer[8];
    file.setPosition(0);
    file.readBuffer(buffer, 8);
    outf("contents: [%s]", esc(buffer, 8).c_str());

    file.setSize(0);
    outf("setSize 0: size=%s", intToStr(file.getSize()).c_str());
    file.close();

    // Reopening for write positions at the start, not at the end: a second
    // writer overwrites rather than appending.
    {
        FileStream first(path, FM_CREATE);
        first.write("AAAA", 4);
    }
    {
        FileStream second(path, FM_OPEN_WRITE);
        outf("reopened for write: pos=%s size=%s",
            intToStr(second.getPosition()).c_str(), intToStr(second.getSize()).c_str());
        second.write("B", 1);
    }
    Buffer check;
    check.loadFromFile(path);
    outf("final contents: [%s]", esc(check.data(), check.getSize()).c_str());
}

DRV_CASE(streams_file_large_roundtrip)
{
    // Large enough that the read comes back in more than one chunk if the
    // implementation ever loops, and not so large that it is slow.
    std::string path = drv::tmpPath("large.bin");
    const int SIZE = 1024 * 1024;

    std::string payload;
    payload.resize(SIZE);
    for (int i = 0; i < SIZE; i++)
        payload[i] = static_cast<char>((i * 31 + 7) & 0xFF);

    {
        FileStream file(path, FM_CREATE);
        outf("write 1 MiB -> %d", file.write(payload.data(), SIZE));
        outf("size = %s", intToStr(file.getSize()).c_str());
    }

    {
        FileStream file(path, FM_OPEN_READ);
        std::string back;
        back.resize(SIZE);
        file.readBuffer(&back[0], SIZE);
        outf("round-tripped intact: %d", (back == payload ? 1 : 0));
    }

    MemoryStream memory;
    memory.loadFromFile(path);
    outf("via MemoryStream: size=%s", intToStr(memory.getSize()).c_str());

    Buffer buffer;
    buffer.loadFromFile(path);
    outf("via Buffer: size=%d first=%s last=%s",
        buffer.getSize(), esc(buffer.data(), 1).c_str(),
        esc(buffer.data() + SIZE - 1, 1).c_str());
}
