///////////////////////////////////////////////////////////////////////////////
// The instruction-count workload.
//
// The task line asks for "instruction counts within a calibrated band of the
// C++98 baseline". This is the workload that count is taken over: a fixed
// number of passes through the library's hot value types -- string formatting
// and parsing, the growable Buffer, the string list, the in-memory stream --
// with no clock, no files, no sockets and no threads, so the only thing that
// moves the count is the code that was migrated.
//
// It is not part of the scored transcript. runAllCases() never reaches it;
// only ISEDRV_BENCH=1 does. A migration that made the library ten times
// slower would otherwise be invisible to a suite that only compares bytes.
//
// The checksum at the end is not an expectation -- the behaviour suite already
// covers these calls. It is here so the work cannot be optimised away, and so
// that a bench that silently stopped doing anything shows up as a changed line
// rather than as a suspiciously low count.
//
// Written in the same C++98-and-C++26 subset as the rest of the driver.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <string>

using namespace ise;

namespace {

// Chosen so the workload runs long enough for the count to be dominated by
// the library rather than by process start-up, and short enough that
// callgrind's ~50x slowdown still fits the step's verifier budget. Calibrated
// against the step 1 golden; see NOTES-authoring.md.
const int BENCH_PASSES = 3000;

UINT bench_hash(UINT accumulator, const string& value)
{
    // FNV-1a over the bytes. The driver's own arithmetic, not the library's,
    // so a change in the count is a change in the library.
    for (size_t i = 0; i < value.size(); i++)
    {
        accumulator ^= static_cast<unsigned char>(value[i]);
        accumulator *= 16777619u;
    }
    return accumulator;
}

UINT bench_format(int pass)
{
    UINT hash = 2166136261u;
    for (int i = 0; i < 24; i++)
    {
        hash = bench_hash(hash, formatString(
            "%d/%s/%08x/%.3f", pass * 24 + i, "field", (pass ^ i) & 0xFFFF,
            static_cast<double>(i) / 8.0));
        hash = bench_hash(hash, intToStr(pass * 1000 + i));
        hash = bench_hash(hash, floatToStr(static_cast<double>(i) * 1.25));
        hash += static_cast<UINT>(strToInt(intToStr(pass + i)));
        hash += static_cast<UINT>(strToInt64(intToStr(static_cast<INT64>(pass) * 7 + i)));
    }
    return hash;
}

UINT bench_strings(int pass)
{
    UINT hash = 2166136261u;
    string line = formatString(
        "name%d = alpha,beta,gamma,delta,epsilon; \"quoted %d\"", pass, pass);

    hash = bench_hash(hash, trimString("   " + line + "   "));
    hash = bench_hash(hash, upperCase(line));
    hash = bench_hash(hash, lowerCase(line));
    hash = bench_hash(hash, repalceString(line, "a", "A"));
    hash = bench_hash(hash, getQuotedStr(line.c_str()));
    hash = bench_hash(hash, getDequotedStr(getQuotedStr(line.c_str()).c_str()));

    StrList parts;
    splitString(line, ',', parts);
    for (int i = 0; i < parts.getCount(); i++)
        hash = bench_hash(hash, parts[i]);

    string rest = line;
    while (!rest.empty())
        hash = bench_hash(hash, fetchStr(rest, ',', true));

    return hash;
}

UINT bench_buffer(int pass)
{
    UINT hash = 2166136261u;
    Buffer buffer;
    // Grows across the 1 KiB and 4 KiB steps the packet path uses, so the
    // count covers the reallocation policy and not only the copies.
    for (int size = 1; size <= 4096; size *= 2)
    {
        buffer.setSize(size, true);
        for (int i = 0; i < size; i += 64)
            buffer[i] = static_cast<char>((pass + i) & 0x7F);
        for (int i = 0; i < size; i += 64)
            hash = (hash ^ static_cast<unsigned char>(buffer[i])) * 16777619u;
    }

    MemoryStream stream;
    string chunk = formatString("record %d ", pass);
    for (int i = 0; i < 64; i++)
        stream.write(chunk.c_str(), static_cast<int>(chunk.size()));
    stream.setPosition(0);
    Buffer readback(static_cast<int>(stream.getSize()));
    stream.read(readback.data(), readback.getSize());
    for (int i = 0; i < readback.getSize(); i += 16)
        hash = (hash ^ static_cast<unsigned char>(readback[i])) * 16777619u;

    return hash;
}

UINT bench_properties(int pass)
{
    UINT hash = 2166136261u;
    PropertyList props;
    for (int i = 0; i < 32; i++)
        props.add(formatString("key%d", i).c_str(),
                  formatString("value-%d-%d", pass, i).c_str());
    for (int i = 0; i < 32; i++)
    {
        string value;
        if (props.getValue(formatString("key%d", i).c_str(), value))
            hash = bench_hash(hash, value);
    }
    hash = bench_hash(hash, props.getPropString());
    return hash;
}

} // namespace

namespace drv {

void runBench()
{
    UINT hash = 2166136261u;
    for (int pass = 0; pass < BENCH_PASSES; pass++)
    {
        hash ^= bench_format(pass);
        hash ^= bench_strings(pass);
        hash ^= bench_buffer(pass);
        hash ^= bench_properties(pass);
    }
    outf("bench passes=%d checksum=%08x", BENCH_PASSES, hash);
}

} // namespace drv
