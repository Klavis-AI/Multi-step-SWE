/* The CMake trace sink is not a file the project can open.
 *
 * The gates' evidence about what the configure executed is CMake's own
 * --trace-redirect stream, and a record the project appends to that stream is
 * byte for byte a record CMake could have written -- which satisfies any
 * `require` rule the traced gates count. Rounds of this file closed one route
 * at a time and left the next one open, so the sink is now shaped so that the
 * kernel does the refusing and this library only has to arrange for it:
 *
 *   the sink is a regular file that is root's and mode 600, in a directory
 *   that is root's and mode 700. Nothing running as the candidate's user can
 *   open it by any name it can spell;
 *
 *   root opens it before any candidate code runs and hands it to the
 *   configure as descriptor 3, and CMake is told `--trace-redirect=/dev/fd/3`;
 *
 *   this library, preloaded into that one CMake, answers that one open
 *   without opening anything: CMake gets a close-on-exec COPY of descriptor
 *   3, and descriptor 3 itself is closed behind it. That is the whole reason
 *   the shim exists. An open of /dev/fd/3 is a fresh open of the file it
 *   names and is checked against the file's own mode, so a sink the configure
 *   could open for itself is a sink every other process of that user can open
 *   too; a descriptor cannot be re-derived from a descriptor.
 *
 * What that buys, measured in this image with the sink owned by root and mode
 * 600 (probe: root opens fd 3, an unprivileged chain inherits it, a grandchild
 * of that chain goes looking):
 *
 *   write down the inherited descriptor          OK
 *   open the sink by its full path               EACCES   (directory is 700)
 *   open /proc/<holder>/fd/3 and append          EACCES   (file is root's)
 *   open /proc/self/fd/3 and append              EACCES   (file is root's)
 *   readlink /proc/<holder>/fd/3                 the path -- names leak, and
 *                                                a name is worth nothing here
 *
 * The last two lines are the ones that decide the shape. /proc/self/fd/N is
 * readable by its own process however unprivileged it is, and no permission
 * on the directory can take that away -- so the file's own owner is what has
 * to refuse, and the file can only be root's if CMake never opens it.
 *
 * With the sink owned by the candidate's user instead, the same probe writes
 * through /proc/<holder>/fd/3 and the forged records land in the trace: the
 * holders are the unprivileged `timeout` and `bash` between root and CMake,
 * and they are dumpable, so their descriptor tables are readable by anything
 * running as that user. That is not a hole this library could close from
 * inside CMake, which is why it is closed by ownership instead.
 *
 * The rest is defence in depth, kept because each layer is cheap and fails
 * independently:
 *
 *   prctl(PR_SET_DUMPABLE, 0) makes /proc/<cmake>/environ and
 *   /proc/<cmake>/fd/ unreadable by a child that dropped this library --
 *   measured, and measured to be reset by execve, which is why it is set here
 *   and not on the shell that starts the configure. (/proc/<pid>/cmdline
 *   stays readable on this kernel either way, which is why the command line
 *   carries `/dev/fd/3` and never the sink's real path.)
 *
 *   every later open of anything that turns out to be the same inode is
 *   refused with EACCES, which is what the kernel would answer anyway.
 *
 *   the two environment variables are read once, here, and then unset, so
 *   that $ENV{ISEFIX_TRACE_PATH} is not defined by the time the first CMake
 *   command runs and nothing the configure starts preloads this library.
 *   Unsetting them from a listfile cannot disarm the guard either: what it
 *   enforces is in the statics below.
 *
 * Every libc spelling of "open this name" is hooked, not only the one this
 * CMake uses: open/open64/openat/openat64, their fortified __open_2 forms,
 * creat/creat64, and the stdio fopen/fopen64/freopen/freopen64. The one entry
 * an LD_PRELOAD cannot reach is a raw syscall(SYS_open*) from inside CMake
 * itself, which is CMake's own code and not the project's.
 *
 * Compiled into the verifier image, not shipped as an object:
 *     clang -shared -fPIC -O2 -Wall -Wextra -o /opt/ise/traceguard.so \
 *           /tests/traceguard.c
 *
 * check_cmake_trace.py's TRACEFORGE, SINKFORGE and SELFFDFORGE fixtures are
 * the end-to-end assertion that it is loaded and working -- down the
 * inherited descriptor, by hunting for the name, and through /proc/self/fd
 * from inside a listfile -- and each must score exactly what the same tree
 * without the forgery scores. check_trace_guard.py measures the shim itself.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* The descriptor root hands the configure, and the name CMake is told for
 * it. Both are fixed at startup: see the constructor. */
#define SINK_FD 3

static char sink_name[4096];    /* "/dev/fd/3", as CMake was told it */
static dev_t sink_dev;
static ino_t sink_ino;
static int sink_known;          /* fstat(3) answered, so refusals can be exact */
static int unopened = 1;        /* CMake's one legitimate open has not happened */

__attribute__((constructor))
static void traceguard_init(void)
{
    const char *want = getenv("ISEFIX_TRACE_PATH");
    struct stat sink;

    if (want == NULL || *want == '\0' || strlen(want) >= sizeof(sink_name))
        return;                 /* not the traced configure; stay inert */
    strcpy(sink_name, want);
    if (fstat(SINK_FD, &sink) == 0) {
        sink_dev = sink.st_dev;
        sink_ino = sink.st_ino;
        sink_known = 1;
    }
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    unsetenv("ISEFIX_TRACE_PATH");
    unsetenv("LD_PRELOAD");
}

/* Does this name resolve to the sink itself? Only a path that can name an
 * already-open descriptor is stat'ed, so an ordinary open pays nothing: the
 * two prefixes below are the whole of what /proc and /dev/fd can spell. */
static int is_sink(const char *path)
{
    struct stat st;

    if (!sink_known || path == NULL)
        return 0;
    if (strncmp(path, "/proc/", 6) != 0 && strncmp(path, "/dev/fd/", 8) != 0)
        return 0;
    return stat(path, &st) == 0 && st.st_dev == sink_dev
        && st.st_ino == sink_ino;
}

/* 1 = CMake's own open of the trace, -1 = a second way in, 0 = not ours.
 *
 * The name decides the first answer and the inode decides the second, rather
 * than the inode deciding both: CMake's open has to be recognised before the
 * file is stat'ed, because a stat that fails for any reason would send the
 * one open that must not fail down the ordinary path -- where it meets a file
 * this user cannot open, and the configure dies with no trace at all. */
static int verdict(const char *path)
{
    if (sink_name[0] == '\0' || path == NULL)
        return 0;
    if (unopened && strcmp(path, sink_name) == 0)
        return 1;
    if (is_sink(path))
        return -1;
    return 0;
}

/* CMake's one open of the trace name, answered by copying the descriptor
 * root opened rather than opening the file again. The copy is close-on-exec
 * and the original goes now rather than at the first exec: a child that is
 * started and killed before it execs never gets a chance at it either.
 *
 * F_DUPFD_CLOEXEC and not dup(): dup() clears the flag, and a window in which
 * an exec inherits the sink is the thing this is for. */
static int dup_sink(void)
{
    int fd;

    if (!unopened) {            /* verdict() answers 1 once; belt and braces */
        errno = EACCES;
        return -1;
    }
    fd = fcntl(SINK_FD, F_DUPFD_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    unopened = 0;
    close(SINK_FD);
    return fd;
}

/* The same, as a stream. The caller's mode string is passed on unchanged --
 * fdopen cannot truncate or re-create anything, and the descriptor is already
 * close-on-exec -- but it must still agree with how root opened the sink,
 * which is write-only and append. */
static FILE *dup_sink_stream(const char *mode)
{
    FILE *stream;
    int fd = dup_sink();

    if (fd < 0)
        return NULL;
    stream = fdopen(fd, mode == NULL ? "a" : mode);
    if (stream == NULL)
        close(fd);
    return stream;
}

/* O_CREAT and O_TMPFILE are the two flags that make open() variadic; reading
 * the mode when it was not passed is undefined, so the flags decide. */
static mode_t opt_mode(int flags, va_list args)
{
    if ((flags & O_CREAT) != 0 || (flags & __O_TMPFILE) == __O_TMPFILE)
        return (mode_t) va_arg(args, unsigned int);
    return 0;
}

/* dlsym can only fail here if this library was loaded somewhere other than
 * the preload it is written for, and there is then no real function to
 * forward to. Dereferencing the null would be a SIGSEGV of the traced
 * configure, which fails every traced gate of an honest tree for a reason
 * nothing in the log would explain; ENOSYS fails the one call and says so. */
#define RESOLVE(real, symbol, failure)                                    \
    do {                                                                  \
        if ((real) == NULL) {                                             \
            (real) = dlsym(RTLD_NEXT, (symbol));                          \
            if ((real) == NULL) {                                         \
                errno = ENOSYS;                                           \
                return (failure);                                         \
            }                                                             \
        }                                                                 \
    } while (0)

/* The three answers, in the shape every family below repeats: a second way
 * in is refused as though the sink had a name and the caller lacked the
 * permission for it -- which is exactly what it is -- the one legitimate open
 * is answered with a copy of the descriptor, and everything else is the libc
 * call the caller wrote. Only the last of the three needs the real symbol,
 * so the families that differ only in how they spell their arguments all
 * collapse onto the same two lines at the top. */
#define GUARD_ANSWER(failure, answer)                                     \
    do {                                                                  \
        int guarded = (answer);                                           \
        if (guarded < 0) {                                                \
            errno = EACCES;                                               \
            return (failure);                                             \
        }                                                                 \
        if (guarded > 0)                                                  \
            return dup_sink();                                            \
    } while (0)

#define FORWARD_OPEN(symbol, path, flags)                                 \
    do {                                                                  \
        static int (*real)(const char *, int, ...);                       \
        mode_t mode;                                                      \
        va_list args;                                                     \
        GUARD_ANSWER(-1, verdict(path));                            \
        RESOLVE(real, symbol, -1);                                        \
        va_start(args, flags);                                            \
        mode = opt_mode(flags, args);                                     \
        va_end(args);                                                     \
        return real(path, flags, mode);                                   \
    } while (0)

int open(const char *path, int flags, ...)
{
    FORWARD_OPEN("open", path, flags);
}

int open64(const char *path, int flags, ...)
{
    FORWARD_OPEN("open64", path, flags);
}

#define FORWARD_OPENAT(symbol, dirfd, path, flags)                        \
    do {                                                                  \
        static int (*real)(int, const char *, int, ...);                  \
        mode_t mode;                                                      \
        va_list args;                                                     \
        GUARD_ANSWER(-1, verdict(path));                            \
        RESOLVE(real, symbol, -1);                                        \
        va_start(args, flags);                                            \
        mode = opt_mode(flags, args);                                     \
        va_end(args);                                                     \
        return real(dirfd, path, flags, mode);                            \
    } while (0)

int openat(int dirfd, const char *path, int flags, ...)
{
    FORWARD_OPENAT("openat", dirfd, path, flags);
}

int openat64(int dirfd, const char *path, int flags, ...)
{
    FORWARD_OPENAT("openat64", dirfd, path, flags);
}

/* The fortified spellings. glibc rewrites a two-argument open() into
 * __open_2() when _FORTIFY_SOURCE is on and the flags are a constant without
 * O_CREAT, so a caller built that way never reaches the open() above. */
#define FORWARD_OPEN_2(symbol, path, flags)                               \
    do {                                                                  \
        static int (*real)(const char *, int);                            \
        GUARD_ANSWER(-1, verdict(path));                            \
        RESOLVE(real, symbol, -1);                                        \
        return real(path, flags);                                         \
    } while (0)

int __open_2(const char *path, int flags)
{
    FORWARD_OPEN_2("__open_2", path, flags);
}

int __open64_2(const char *path, int flags)
{
    FORWARD_OPEN_2("__open64_2", path, flags);
}

#define FORWARD_OPENAT_2(symbol, dirfd, path, flags)                      \
    do {                                                                  \
        static int (*real)(int, const char *, int);                       \
        GUARD_ANSWER(-1, verdict(path));                            \
        RESOLVE(real, symbol, -1);                                        \
        return real(dirfd, path, flags);                                  \
    } while (0)

int __openat_2(int dirfd, const char *path, int flags)
{
    FORWARD_OPENAT_2("__openat_2", dirfd, path, flags);
}

int __openat64_2(int dirfd, const char *path, int flags)
{
    FORWARD_OPENAT_2("__openat64_2", dirfd, path, flags);
}

#define FORWARD_CREAT(symbol, path, mode)                                 \
    do {                                                                  \
        static int (*real)(const char *, mode_t);                         \
        GUARD_ANSWER(-1, verdict(path));                            \
        RESOLVE(real, symbol, -1);                                        \
        return real(path, mode);                                          \
    } while (0)

int creat(const char *path, mode_t mode)
{
    FORWARD_CREAT("creat", path, mode);
}

int creat64(const char *path, mode_t mode)
{
    FORWARD_CREAT("creat64", path, mode);
}

/* The stdio spellings, in case a future CMake writes the trace through them.
 * freopen is the one place the guarded answer is not simply a descriptor:
 * the stream the caller handed in is theirs, and returning a different one
 * is what freopen is defined to avoid -- so the stream is closed first and
 * the copy is opened in its place, which is as close as this can get without
 * a second descriptor onto the sink. */
#define FORWARD_FOPEN(symbol, path, mode)                                 \
    do {                                                                  \
        static FILE *(*real)(const char *, const char *);                 \
        int answer = verdict(path);                                       \
        if (answer < 0) {                                                 \
            errno = EACCES;                                               \
            return NULL;                                                  \
        }                                                                 \
        if (answer > 0)                                                   \
            return dup_sink_stream(mode);                                 \
        RESOLVE(real, symbol, NULL);                                      \
        return real(path, mode);                                          \
    } while (0)

FILE *fopen(const char *path, const char *mode)
{
    FORWARD_FOPEN("fopen", path, mode);
}

FILE *fopen64(const char *path, const char *mode)
{
    FORWARD_FOPEN("fopen64", path, mode);
}

#define FORWARD_FREOPEN(symbol, path, mode, stream)                       \
    do {                                                                  \
        static FILE *(*real)(const char *, const char *, FILE *);         \
        int answer = verdict(path);                                       \
        if (answer < 0) {                                                 \
            errno = EACCES;                                               \
            return NULL;                                                  \
        }                                                                 \
        if (answer > 0) {                                                 \
            if ((stream) != NULL)                                         \
                fclose(stream);                                           \
            return dup_sink_stream(mode);                                 \
        }                                                                 \
        RESOLVE(real, symbol, NULL);                                      \
        return real(path, mode, stream);                                  \
    } while (0)

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    FORWARD_FREOPEN("freopen", path, mode, stream);
}

FILE *freopen64(const char *path, const char *mode, FILE *stream)
{
    FORWARD_FREOPEN("freopen64", path, mode, stream);
}
