///////////////////////////////////////////////////////////////////////////////
// A characterisation driver for the parts of ise/main that no example
// program reaches.
//
// This file is compiled by the verifier against the tree under /app. It is
// never copied into /app, so the agent being scored does not see it. What it
// does see is the instruction's statement that the public API of ise/main is
// frozen for the whole migration line -- which is the same statement this
// driver makes, in the only form a compiler can check.
//
// Deliberately written in the subset of C++ that both -std=gnu++98 and
// -std=c++26 accept, so the identical source compiles against the baseline
// and against every migrated tree.
///////////////////////////////////////////////////////////////////////////////

#ifndef ISE_DRV_H
#define ISE_DRV_H

#include <string>
#include <vector>

namespace drv {

typedef void (*CaseFn)();

struct Case
{
    const char *name;
    CaseFn fn;
    // Non-NULL for a case whose output is not a function of its inputs: the
    // library either has undefined behaviour on that path or reads the wall
    // clock. Such a block is printed -- a reader of the log wants to see it --
    // but the recorder drops it and the stability check ignores it, because an
    // expectation taken from it would be a coin toss a correct migration
    // could lose. The reason is printed next to the name so the two can never
    // drift apart.
    const char *unscored;
};

std::vector<Case>& registry();

struct Register
{
    Register(const char *name, CaseFn fn, const char *unscored)
    {
        Case entry;
        entry.name = name;
        entry.fn = fn;
        entry.unscored = unscored;
        registry().push_back(entry);
    }
};

// One output line of the case currently running.
void out(const std::string& line);

// printf-style convenience; the formatting itself is ise's own.
void outf(const char *format, ...);

// A C-escaped rendering, so a case that produces control bytes or invalid
// UTF-8 still produces one printable line.
std::string esc(const std::string& value);
std::string esc(const char *data, int size);

// Where a case may create files. Emptied before the run.
std::string tmpDir();
std::string tmpPath(const std::string& name);

// The instruction-count workload, in bench.cpp. Reached only by setting
// ISEDRV_BENCH in the environment, so it is never part of the scored
// transcript and costs a behaviour replay nothing.
void runBench();

} // namespace drv

// The number of elements in an array, for loop bounds. A hand-written bound
// that outgrows its table reads past the end and prints whatever was next in
// the image -- which is a difference between two runs of the same library.
#define DRV_COUNT(array) (int)(sizeof(array) / sizeof((array)[0]))

// An enumerator of one of the library's enums, spelled so that it compiles
// whether or not that enum has been scoped yet. In C++11 an unscoped enum's
// name is also a scope and a scoped enum's name is the only scope, so `E::V`
// is right either way; in C++98 only the bare `V` is.
//
// The driver is copied unchanged into every step and the steps do not agree
// about either half of that: step 1 builds it at gnu++98 against a tree with
// unscoped enums, the last step at c++26 against a tree without them. These
// macros are the only reason one file can be both.
//
// Both forms resolve their leading name through the `using namespace ise;`
// every translation unit here opens with -- the same directive that lets the
// driver write `StrList` and `Url` -- so neither spells the namespace out.
#if __cplusplus >= 201103L
#  define DRV_ENUM(E, V) E::V
#  define DRV_CLASS_ENUM(C, E, V) C::E::V
#else
#  define DRV_ENUM(E, V) V
#  define DRV_CLASS_ENUM(C, E, V) C::V
#endif

// The same enumerator where an integer is wanted: a `UINT mask` table, three
// flags `|`-ed together, a `static const int[]`. A scoped enum does not
// convert on its own and an unscoped one does, so writing the cast
// unconditionally is what makes the two trees agree. Every enumerator these
// six enums declare is a small non-negative constant, so the value survives
// the round trip into `int` and back out into whatever the site wants --
// including the narrowing rules a braced initialiser applies, which permit a
// constant expression whose value fits. Only the class form exists: every
// integer context in this driver is on `Url::URL_PART` or
// `StrList::DUPLICATE_MODE`.
#define DRV_CLASS_ENUM_INT(C, E, V) \
    (static_cast<int>(DRV_CLASS_ENUM(C, E, V)))

// For the few helpers whose whole point is the stack frame they occupy.
// Both toolchains the driver is built with understand it; there is no
// standard spelling, and [[gnu::noinline]] is not available in C++98.
#define ISE_NOINLINE __attribute__((noinline))

#define DRV_CASE(name)                                                    \
    static void drv_fn_##name();                                          \
    static drv::Register drv_reg_##name(#name, drv_fn_##name, 0);         \
    static void drv_fn_##name()

// A case that is run and printed but never turned into an expectation. The
// reason is not decoration: it is the one place a later reader is told why an
// obvious piece of the API is missing from the scored set.
#define DRV_UNSCORED_CASE(name, reason)                                   \
    static void drv_fn_##name();                                          \
    static drv::Register drv_reg_##name(#name, drv_fn_##name, reason);    \
    static void drv_fn_##name()

#endif // ISE_DRV_H
