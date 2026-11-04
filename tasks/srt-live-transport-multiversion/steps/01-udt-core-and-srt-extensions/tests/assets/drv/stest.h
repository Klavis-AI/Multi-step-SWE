// Minimal group/case test framework for the SRT verifier drivers.
//
// Two properties the shipped harness depends on:
//
//   * `--group NAME` runs exactly the cases registered under NAME and prints
//     `EXECUTED <n> FAILED <m>`. test.sh asserts n against the count declared
//     in groups.txt, so a filter that matches nothing is a failure rather than
//     an empty pass.
//   * every case runs in a forked child with its own alarm. SRT keeps
//     process-global state (srt_startup/srt_cleanup plus worker threads), so
//     running cases in one process would let one case's leaked socket change
//     another's result -- and a segfault or a hung handshake would lose the
//     whole group instead of one case.
#pragma once

#include <srt/srt.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

struct STCase {
    const char* group;
    const char* name;
    void (*fn)();
};

inline std::vector<STCase>& st_registry() {
    static std::vector<STCase> r;
    return r;
}

struct STReg {
    STReg(const char* g, const char* n, void (*f)()) { st_registry().push_back({g, n, f}); }
};

// SRT_CASE(group_identifier, case_identifier) { ... }
#define SRT_CASE(grp, nm)                                                    \
    static void st_fn_##grp##_##nm();                                        \
    static STReg st_reg_##grp##_##nm(#grp, #nm, st_fn_##grp##_##nm);         \
    static void st_fn_##grp##_##nm()

// Assertions throw; the child catches, prints and exits non-zero.
struct STFail : std::runtime_error {
    explicit STFail(const std::string& m) : std::runtime_error(m) {}
};

inline void st_failf(const char* file, int line, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    char out[1200];
    snprintf(out, sizeof out, "%s:%d: %s", file, line, buf);
    throw STFail(out);
}

#define ST_REQUIRE(cond)                                                     \
    do {                                                                     \
        if (!(cond)) st_failf(__FILE__, __LINE__, "requirement failed: %s", #cond); \
    } while (0)

#define ST_REQUIRE_MSG(cond, ...)                                            \
    do {                                                                     \
        if (!(cond)) st_failf(__FILE__, __LINE__, __VA_ARGS__);              \
    } while (0)

// Most SRT calls return SRT_ERROR and stash the reason globally; report it.
#define ST_SRT_OK(expr)                                                      \
    do {                                                                     \
        if ((expr) == SRT_ERROR)                                             \
            st_failf(__FILE__, __LINE__, "%s failed: %s", #expr, srt_getlasterror_str()); \
    } while (0)

#define ST_REQUIRE_EQ_INT(actual, expected)                                  \
    do {                                                                     \
        long long a_ = (long long)(actual), e_ = (long long)(expected);      \
        if (a_ != e_)                                                        \
            st_failf(__FILE__, __LINE__, "%s: expected %lld, got %lld", #actual, e_, a_); \
    } while (0)

inline int st_case_timeout_seconds() {
    const char* e = getenv("SRT_CASE_TIMEOUT");
    int v = e ? atoi(e) : 0;
    return v > 0 ? v : 60;
}

// The per-case deadline, and what happens when it expires.
//
// The deadline exists to stop one wedged case from taking the whole run down,
// so it has to cover the case body. It must NOT cover what comes after the
// verdict. srt_cleanup() on a connection that carried thousands of messages
// can take seconds on a loaded host, and a SIGALRM landing in there would kill
// a child that had already printed PASS -- the parent would see WIFSIGNALED
// and report "timed out" for a case whose every assertion had held, and the
// group log would carry the same case twice, PASS and then FAIL, which is not
// a result anyone can act on.
//
// So the handler is installed once, before the body, and reads a flag the
// child sets the instant the verdict is printed:
//
//   flag clear -- the body itself overran. Restore the default disposition
//                 and re-raise, so the parent still sees a genuine SIGALRM
//                 death and still names it a timeout. Nothing changes for a
//                 case that really is wedged.
//   flag set   -- teardown overran. Exit with the verdict already reached.
//                 The measurement is over; teardown cannot overturn it.
//
// Installing the handler up front rather than at verdict time is what makes
// this race-free: there is no window in which the old disposition is still
// SIG_DFL but the verdict has already been printed.
enum { ST_TEARDOWN_GRACE_S = 30 };

inline int& st_verdict_rc() {
    static int rc = 0;
    return rc;
}

inline volatile std::sig_atomic_t& st_verdict_printed() {
    static volatile std::sig_atomic_t printed = 0;
    return printed;
}

inline void st_deadline_expired(int) {
    if (st_verdict_printed()) _exit(st_verdict_rc());
    std::signal(SIGALRM, SIG_DFL);
    std::raise(SIGALRM);
}

// Runs one case in a child process. Returns 0 on pass.
inline int st_run_one(const STCase& c) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        std::printf("CASE %s.%s FAIL fork: %s\n", c.group, c.name, std::strerror(errno));
        return 1;
    }
    if (pid == 0) {
        // Touch both accessors before arming, so the handler can never be the
        // caller that runs a function-local static's guarded initialisation.
        st_verdict_rc() = 0;
        st_verdict_printed() = 0;
        std::signal(SIGALRM, st_deadline_expired);
        alarm((unsigned)st_case_timeout_seconds());
        int rc = 0;
        if (srt_startup() == SRT_ERROR) {
            std::printf("CASE %s.%s FAIL srt_startup: %s\n", c.group, c.name, srt_getlasterror_str());
            std::fflush(stdout);
            _exit(1);
        }
        try {
            c.fn();
            std::printf("CASE %s.%s PASS\n", c.group, c.name);
        } catch (const STFail& e) {
            std::printf("CASE %s.%s FAIL %s\n", c.group, c.name, e.what());
            rc = 1;
        } catch (const std::exception& e) {
            std::printf("CASE %s.%s FAIL unexpected exception: %s\n", c.group, c.name, e.what());
            rc = 1;
        } catch (...) {
            std::printf("CASE %s.%s FAIL unexpected non-standard exception\n", c.group, c.name);
            rc = 1;
        }
        std::fflush(stdout);
        // The verdict is on stdout and the case is no longer being measured.
        // Hand the remaining deadline to teardown, which may only end this
        // child with the verdict above -- never by signal.
        st_verdict_rc() = rc;
        st_verdict_printed() = 1;
        alarm((unsigned)ST_TEARDOWN_GRACE_S);
        srt_cleanup();
        _exit(rc);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        std::printf("CASE %s.%s FAIL waitpid: %s\n", c.group, c.name, std::strerror(errno));
        return 1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status) == 0 ? 0 : 1;
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        // SIGALRM is this framework's own per-case deadline, so name it as a
        // timeout rather than as an anonymous signal death.
        if (sig == SIGALRM)
            std::printf("CASE %s.%s FAIL timed out after %d s\n", c.group, c.name,
                        st_case_timeout_seconds());
        else
            std::printf("CASE %s.%s FAIL killed by signal %d (%s)\n", c.group, c.name, sig,
                        strsignal(sig));
        return 1;
    }
    std::printf("CASE %s.%s FAIL child ended abnormally\n", c.group, c.name);
    return 1;
}

inline int st_main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const char* group = nullptr;
    bool list = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--group") && i + 1 < argc) group = argv[++i];
        else if (!std::strcmp(argv[i], "--list")) list = true;
        else {
            std::fprintf(stderr, "usage: %s [--group NAME | --list]\n", argv[0]);
            return 2;
        }
    }
    if (list) {
        // One line per group with its case count, the value groups.txt
        // declares in its case-count column, so the declared counts can be
        // checked against the cases that actually exist.
        std::vector<std::string> seen;
        for (const auto& c : st_registry()) {
            bool found = false;
            for (auto& s : seen)
                if (s == c.group) { found = true; break; }
            if (!found) seen.push_back(c.group);
        }
        for (const auto& g : seen) {
            int n = 0;
            for (const auto& c : st_registry())
                if (g == c.group) n++;
            std::printf("%s %d\n", g.c_str(), n);
        }
        return 0;
    }
    if (!group) {
        std::fprintf(stderr, "no --group given\n");
        return 2;
    }
    int executed = 0, failed = 0;
    for (const auto& c : st_registry()) {
        if (std::strcmp(c.group, group) != 0) continue;
        executed++;
        if (st_run_one(c) != 0) failed++;
    }
    std::printf("EXECUTED %d FAILED %d\n", executed, failed);
    return failed == 0 && executed > 0 ? 0 : 1;
}

int main(int argc, char** argv) { return st_main(argc, argv); }
