/* launch.c -- verifier-owned launcher for the candidate's utilities.
 *
 *   launch UID PROGRAM [ARGS...]
 *
 * Run as root by runner.py, in place of `setpriv --reuid=UID --regid=UID
 * --clear-groups PROGRAM ARGS...`.  It drops to UID exactly as setpriv does, then
 * installs a seccomp filter before the one execve that starts PROGRAM:
 *
 *   - execve is allowed only for the filename pointer this launcher passes, which
 *     lives at an unpredictable address in a page that vanishes with the exec, so
 *     PROGRAM itself can start no other program (no trampoline to another
 *     implementation, no ld.so indirection, no interpreter);
 *   - execveat, ptrace, process_vm_readv/writev and io_uring_setup are refused;
 *   - sockets of the inet, inet6 and packet families are refused, so nothing can
 *     be fetched while a case runs;
 *   - any non-x86_64 system call ABI kills the process.
 *
 * Exit status mirrors setpriv: 127 when PROGRAM does not exist, 126 when it cannot
 * be executed or the launcher cannot set itself up.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#if !defined(__x86_64__)
#error "launch.c filters the x86_64 system call ABI only"
#endif

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS 0x80000000U
#endif
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif

extern char **environ;

#define PAGE_SPAN 8192u

static int die(const char *what) {
    fprintf(stderr, "launch: %s: %s\n", what, strerror(errno));
    return 126;
}

#define LD(off)        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (off))
#define JEQ(k, t, f)   BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (k), (t), (f))
#define JGE(k, t, f)   BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, (k), (t), (f))
#define RET(v)         BPF_STMT(BPF_RET | BPF_K, (v))
#define DENY           RET(SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA))
#define ALLOW          RET(SECCOMP_RET_ALLOW)
#define ARG0_LO        (offsetof(struct seccomp_data, args[0]))
#define ARG0_HI        (offsetof(struct seccomp_data, args[0]) + 4)

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: launch UID PROGRAM [ARGS...]\n");
        return 126;
    }
    char *end = NULL;
    errno = 0;
    long uid = strtol(argv[1], &end, 10);
    if (errno || !end || *end || uid <= 0 || uid > 0x7fffffffL) {
        fprintf(stderr, "launch: bad uid %s\n", argv[1]);
        return 126;
    }

    /* The privilege drop setpriv performed: no supplementary groups, then gid, then
     * uid, real, effective and saved alike -- and prove root cannot be regained. */
    if (setgroups(0, NULL) != 0) return die("setgroups");
    if (setresgid((gid_t)uid, (gid_t)uid, (gid_t)uid) != 0) return die("setresgid");
    if (setresuid((uid_t)uid, (uid_t)uid, (uid_t)uid) != 0) return die("setresuid");
    if (getuid() != (uid_t)uid || geteuid() != (uid_t)uid ||
        getgid() != (gid_t)uid || getegid() != (gid_t)uid) {
        fprintf(stderr, "launch: privilege drop did not hold\n");
        return 126;
    }
    if (setresuid(0, 0, 0) == 0) {
        fprintf(stderr, "launch: root could be regained after the drop\n");
        return 126;
    }

    size_t n = strlen(argv[2]) + 1;
    if (n > PAGE_SPAN / 2) {
        fprintf(stderr, "launch: program path too long\n");
        return 126;
    }

    /* Two pages at a random address, and the filename at a random 8-byte-aligned
     * offset inside them: the only execve the filter allows is one whose filename
     * argument is exactly that pointer. */
    void *page = MAP_FAILED;
    for (int i = 0; i < 64 && page == MAP_FAILED; i++) {
        uint64_t r = 0;
        if (getrandom(&r, sizeof r, 0) != (ssize_t)sizeof r) return die("getrandom");
        uint64_t lo = 0x100000000000ULL, hi = 0x7e0000000000ULL;
        uintptr_t hint = (uintptr_t)((lo + r % (hi - lo)) & ~(uint64_t)0xfff);
        void *p = mmap((void *)hint, PAGE_SPAN, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == MAP_FAILED) continue;
        if (p != (void *)hint) { munmap(p, PAGE_SPAN); continue; }
        page = p;
    }
    if (page == MAP_FAILED) return die("mmap");
    uint64_t r2 = 0;
    if (getrandom(&r2, sizeof r2, 0) != (ssize_t)sizeof r2) return die("getrandom");
    size_t room = PAGE_SPAN - n;
    size_t off = (size_t)(r2 % (room + 1)) & ~(size_t)7;
    char *path = (char *)page + off;
    memcpy(path, argv[2], n);
    uint64_t a = (uint64_t)(uintptr_t)path;

    struct sock_filter f[] = {
        /* 0 */ LD(offsetof(struct seccomp_data, arch)),
        /* 1 */ JEQ(AUDIT_ARCH_X86_64, 1, 0),
        /* 2 */ RET(SECCOMP_RET_KILL_PROCESS),
        /* 3 */ LD(offsetof(struct seccomp_data, nr)),
        /* 4 */ JGE(0x40000000u, 0, 1),            /* x32 ABI */
        /* 5 */ DENY,
        /* 6 */ JEQ(__NR_execve, 0, 6),
        /* 7 */ LD(ARG0_LO),
        /* 8 */ JEQ((uint32_t)a, 0, 2),
        /* 9 */ LD(ARG0_HI),
        /*10 */ JEQ((uint32_t)(a >> 32), 1, 0),
        /*11 */ DENY,
        /*12 */ ALLOW,
        /*13 */ JEQ(__NR_execveat, 0, 1),
        /*14 */ DENY,
        /*15 */ JEQ(__NR_ptrace, 0, 1),
        /*16 */ DENY,
        /*17 */ JEQ(__NR_process_vm_readv, 0, 1),
        /*18 */ DENY,
        /*19 */ JEQ(__NR_process_vm_writev, 0, 1),
        /*20 */ DENY,
        /*21 */ JEQ(__NR_io_uring_setup, 0, 1),
        /*22 */ DENY,
        /*23 */ JEQ(__NR_socket, 0, 5),
        /*24 */ LD(ARG0_LO),
        /*25 */ JEQ(AF_INET, 2, 0),
        /*26 */ JEQ(AF_INET6, 1, 0),
        /*27 */ JEQ(AF_PACKET, 0, 1),
        /*28 */ DENY,
        /*29 */ ALLOW,
    };
    struct sock_fprog prog = { .len = (unsigned short)(sizeof f / sizeof f[0]), .filter = f };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return die("no_new_privs");
    if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog) != 0 &&
        prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0) != 0)
        return die("seccomp");

    execve(path, argv + 2, environ);
    int e = errno;
    fprintf(stderr, "launch: failed to execute %s: %s\n", argv[2], strerror(e));
    return e == ENOENT ? 127 : 126;
}
