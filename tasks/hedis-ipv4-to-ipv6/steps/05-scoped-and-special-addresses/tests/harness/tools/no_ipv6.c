/* no-ipv6: exec a program on a kernel that appears to have no IPv6.
 *
 * The mirror of `no_ipv4.c`, and it exists for one question: a default that is
 * a *choice of family* and a default that is a *preference to fall back from*
 * behave identically on any host that has IPv6, which is every host this
 * harness runs on. Take IPv6 away and they part company -- the first fails and
 * says so, the second quietly comes up on IPv4.
 *
 * Installs a seccomp filter that fails socket(AF_INET6, ...) with
 * EAFNOSUPPORT -- the errno a kernel built without IPv6 returns -- and leaves
 * every other syscall alone. PR_SET_NO_NEW_PRIVS makes this work
 * unprivileged, and the filter is inherited across fork and exec.
 *
 * Unlike `no_ipv4.c` this is never built into the image: it is compiled at run
 * time by `lib/no6.py`, in both the recorder and the verifier, from this
 * source.
 *
 * usage: no-ipv6 <command> [args...]
 */
#include <errno.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <seccomp.h>

int main(int argc, char **argv) {
    scmp_filter_ctx ctx;

    if (argc < 2) {
        fprintf(stderr, "usage: no-ipv6 <command> [args...]\n");
        return 2;
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        perror("no-ipv6: PR_SET_NO_NEW_PRIVS");
        return 3;
    }
    ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == NULL) {
        fprintf(stderr, "no-ipv6: seccomp_init failed\n");
        return 3;
    }
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EAFNOSUPPORT), SCMP_SYS(socket), 1,
                         SCMP_A0(SCMP_CMP_EQ, AF_INET6)) < 0) {
        fprintf(stderr, "no-ipv6: seccomp_rule_add failed\n");
        return 3;
    }
    if (seccomp_load(ctx) < 0) {
        fprintf(stderr, "no-ipv6: seccomp_load failed\n");
        return 3;
    }
    seccomp_release(ctx);

    execvp(argv[1], argv + 1);
    perror("no-ipv6: execvp");
    return 127;
}
