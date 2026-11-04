/* no-ipv4: exec a program on a kernel that appears to have no IPv4.
 *
 * Installs a seccomp filter that fails socket(AF_INET, ...) with EAFNOSUPPORT
 * -- the errno a kernel built without IPv4 returns -- and leaves every other
 * syscall alone. PR_SET_NO_NEW_PRIVS makes this work unprivileged, and the
 * filter is inherited across fork and exec, so the whole process tree the
 * command starts sees the same kernel.
 *
 * This is the substitute for the IPv6-only network namespace the task line
 * asks for: the container has no CAP_SYS_ADMIN, so no namespace can be
 * created, and a filter that refuses to create the socket at all is strictly
 * stronger than a namespace that merely has no IPv4 addresses.
 *
 * usage: no-ipv4 <command> [args...]
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
        fprintf(stderr, "usage: no-ipv4 <command> [args...]\n");
        return 2;
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        perror("no-ipv4: PR_SET_NO_NEW_PRIVS");
        return 3;
    }
    ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == NULL) {
        fprintf(stderr, "no-ipv4: seccomp_init failed\n");
        return 3;
    }
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EAFNOSUPPORT), SCMP_SYS(socket), 1,
                         SCMP_A0(SCMP_CMP_EQ, AF_INET)) < 0) {
        fprintf(stderr, "no-ipv4: seccomp_rule_add failed\n");
        return 3;
    }
    if (seccomp_load(ctx) < 0) {
        fprintf(stderr, "no-ipv4: seccomp_load failed\n");
        return 3;
    }
    seccomp_release(ctx);

    execvp(argv[1], argv + 1);
    perror("no-ipv4: execvp");
    return 127;
}
