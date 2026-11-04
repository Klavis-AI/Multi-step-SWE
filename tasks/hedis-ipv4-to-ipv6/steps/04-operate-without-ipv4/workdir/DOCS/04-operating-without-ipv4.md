# A host that has no IPv4 — the reference for this workspace

This note collects the parts of the published standards, and the measured
behaviour of this platform, that the work in this workspace rests on. It states
what the standards say and what this container does; it does not state what this
repository must do. The requirements are in the step instruction.

The documents are public. Look them up in full if you want the exact wording:

* **RFC 3493 — Basic Socket Interface Extensions for IPv6**, §6.1 for
  `getaddrinfo()` and `AF_UNSPEC`, §5.3 for `IPV6_V6ONLY`.
  <https://www.rfc-editor.org/rfc/rfc3493>
* **RFC 4038 — Application Aspects of IPv6 Transition**, §3.2 on asking the
  resolver for every address, §5 on porting, §6.1 on version-independent
  structures. <https://www.rfc-editor.org/rfc/rfc4038>
* **RFC 6540 / BCP 177 — IPv6 Support Required for All IP-Capable Nodes.**
  <https://www.rfc-editor.org/rfc/rfc6540>
* **POSIX.1-2017**, the `socket()`, `bind()` and `getaddrinfo()` pages.
  <https://pubs.opengroup.org/onlinepubs/9699919799/>
* `man 2 socket`, `man 7 ipv6`, `man 2 seccomp` and `man 2 prctl` for this
  platform's own wording.

## 1. What "no IPv4" is, in system-call terms

A kernel that does not implement IPv4 — one built without `CONFIG_INET`, or a
process whose system-call policy denies the family — refuses to create the
socket, and nothing else changes:

```
socket(AF_INET,  SOCK_STREAM, 0)  ->  -1, errno = EAFNOSUPPORT  (97 here)
socket(AF_INET6, SOCK_STREAM, 0)  ->  a file descriptor
socket(AF_UNIX,  SOCK_STREAM, 0)  ->  a file descriptor
```

POSIX lists `[EAFNOSUPPORT]` — "The implementation does not support the specified
address family" — among the errors `socket()` **shall** fail with, alongside
`[EPROTONOSUPPORT]`, "The protocol is not supported by the address family, or
the protocol is not supported by the implementation". They are ordinary,
specified outcomes of asking for a family, not exceptional conditions.

Three things are *not* affected, and each of them is a place where a program can
go wrong by assuming otherwise:

* **Name resolution is unchanged.** The resolver answers out of the host's name
  service — `/etc/hosts`, DNS — which knows nothing about the running kernel. A
  name with an address in each family resolves to both. Measured in this
  container, with and without the filter of §3 applied, identically:

  ```
  getaddrinfo("localhost", AF_UNSPEC)  ->  ::1, 127.0.0.1
  ```

* **Interface configuration is unchanged.** `lo` still carries `127.0.0.1`, and
  `/etc/hosts` still maps names to IPv4 addresses. Nothing removes the addresses
  from the host; what has gone is the ability to open a socket for them.

* **`AI_ADDRCONFIG` is not a way to ask this question.** That flag asks which
  families the *host has addresses configured for*, which is a different
  question and, on this platform, gives the opposite of a useful answer.
  Measured here, again identically with and without the filter:

  ```
  getaddrinfo("localhost", AF_UNSPEC | AI_ADDRCONFIG)  ->  127.0.0.1, 127.0.0.1
  ```

  The IPv6 record is gone and the IPv4 one is returned twice. A program that
  reached for this flag to avoid the addresses it cannot use would be left with
  only the addresses it cannot use.

## 2. What a program does about it (RFC 4038 §3.2, RFC 3493 §6.1)

RFC 3493 §6.1 defines the resolution interface as one that returns a **list** of
addresses, with `AF_UNSPEC` meaning "the caller will accept any protocol
family". RFC 4038 §3.2 states the matching obligation on the caller: the
application "should request all IP addresses without address family constraints
and try all the records returned from the DNS".

"Try all the records" is the part that matters on a host without IPv4. A list
that came back from a resolver which knows nothing about the kernel can contain
an address the kernel will not give a socket for, and that is an expected member
of the list rather than a failure of the lookup:

* For a program **connecting** out, trying all the records means an address that
  cannot even be attempted is one the walk moves past, exactly as it moves past
  one that refuses the connection. The operation has failed when the list is
  exhausted, not when a member of it does.
* For a program **listening**, the same list comes back from the same call — a
  configured name is resolved before anything is bound — and the same reasoning
  applies to the sockets it creates from it. The distinction to keep is between
  "this address cannot exist on this kernel" and "this address exists and could
  not be bound": a port already in use, or an address that belongs to no
  interface, is a configuration error and says so. A family the kernel does not
  implement was never something the configuration chose, because no line in a
  configuration file can add or remove a family from a resolver's answer.

RFC 4038 §6.1 puts the general form of it: "All memory structures and APIs
should be IP version-independent", and the document's examples pass
`hints.ai_family = AF_UNSPEC` throughout, take addresses as `sockaddr_storage`,
and use the `ai_addrlen` the resolver returned rather than a length derived from
a family the code picked.

## 3. Reproducing the environment

This container's kernel does implement IPv4, so the condition has to be created
per-process. A seccomp filter that fails `socket(AF_INET, …)` with
`EAFNOSUPPORT` and leaves every other system call alone is indistinguishable,
from inside the process, from a kernel built without IPv4: the same errno from
the same call, and every other family untouched. `PR_SET_NO_NEW_PRIVS` makes it
installable without privilege, and a seccomp filter is inherited across `fork()`
and `execve()`, so every process the command starts runs under it too.

The program is below in full. It needs nothing but a C compiler and the libc
headers, both of which are installed here.

**Build it outside the repository** — `/tmp` is the obvious place. This program
is a property of the machine you are testing on, not of the software: nothing
about it belongs in the tree, and the tree should not gain a file because you
needed one.

```sh
cd /tmp && cc -O2 -o no-ipv4 no-ipv4.c
/tmp/no-ipv4 src/redis-server --port 7777
```

```c
/* no-ipv4: run a command on a kernel that appears to have no IPv4.
 *
 * Installs a seccomp filter that fails socket(AF_INET, ...) with EAFNOSUPPORT
 * -- the errno a kernel built without IPv4 returns -- and leaves every other
 * system call alone. PR_SET_NO_NEW_PRIVS makes it work unprivileged, and the
 * filter is inherited across fork and exec, so every process the command
 * starts sees the same kernel.
 *
 *   cc -O2 -o no-ipv4 no-ipv4.c
 *   ./no-ipv4 src/redis-server --port 7777
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
    struct sock_filter filter[] = {
        /* Anything but this architecture's own calling convention is allowed
         * through rather than guessed at. */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_socket, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_INET, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K,
                 SECCOMP_RET_ERRNO | (EAFNOSUPPORT & SECCOMP_RET_DATA)),
    };
    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    if (argc < 2) {
        fprintf(stderr, "usage: no-ipv4 <command> [args...]\n");
        return 2;
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        perror("no-ipv4: PR_SET_NO_NEW_PRIVS");
        return 3;
    }
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog) != 0) {
        perror("no-ipv4: seccomp");
        return 3;
    }
    execvp(argv[1], argv + 1);
    perror("no-ipv4: execvp");
    return 127;
}
```

A one-line check that it is doing what it says, before trusting anything it
tells you about this repository — the first call must fail and the second must
not:

```sh
/tmp/no-ipv4 python3 -c 'import socket
for f in (socket.AF_INET, socket.AF_INET6):
    try:
        socket.socket(f).close(); print(f, "ok")
    except OSError as e: print(f, e)'
```

## 4. Why a host like that is a host worth running on (BCP 177)

RFC 6540, which is BCP 177, is a two-page document whose whole content is this
requirement: "New IP implementations must support IPv6", "IPv6 support must be
equivalent or better in quality and functionality when compared to IPv4
support", and — the sentence this workspace is about — an implementation may
support both, "but must not require IPv4 for proper and complete function".

RFC 4038 §5 is the practical companion: an application that has been ported is
one that runs unchanged on an IPv6-only node, an IPv4-only node, and a
dual-stack node, because nothing in it decides in advance which of those it is
running on.

## 5. Where this repository's own interface is documented

The behaviour of the programs in this workspace — the configuration directives
they read, the commands they answer and the way they are built — is documented
by the repository itself: `README.md`, the annotated `redis.conf` and
`sentinel.conf`, and the test suite under `tests/`. Those files are the
authority on what this tree already does.
