# What "the default" is, and which addresses are for writing down — the reference for this workspace

This note collects what the published standards and the C library on this
machine say about two things: what a program gets when it does not say which
address to listen on, and which addresses exist for the purpose of appearing in
documentation. It states what is so; it does not state what this repository must
do. The requirements are in the step instruction.

The documents are public. Look them up in full if you want the exact wording:

* **POSIX / RFC 3493 §6.1** for `getaddrinfo` and the `AI_PASSIVE`,
  `AI_V4MAPPED` and `AI_ALL` flags; `man 3 getaddrinfo` on this machine is the
  same text with the glibc extensions.
  <https://www.rfc-editor.org/rfc/rfc3493>
* **RFC 3493 §5.3** and `man 7 ipv6` for the `IPV6_V6ONLY` socket option.
* **RFC 6724 — Default Address Selection for IPv6**, for the order a resolver
  returns addresses in. <https://www.rfc-editor.org/rfc/rfc6724>
* **RFC 3849 — IPv6 Address Prefix Reserved for Documentation**, for
  `2001:db8::/32`. <https://www.rfc-editor.org/rfc/rfc3849>
* **RFC 5737 — IPv4 Address Blocks Reserved for Documentation**, for
  `192.0.2.0/24`, `198.51.100.0/24` and `203.0.113.0/24`.
  <https://www.rfc-editor.org/rfc/rfc5737>
* **RFC 6890 — Special-Purpose IP Address Registries**, for the registry both of
  those live in, and for the IPv4 side of the classes in
  `DOCS/05-address-classes.md`. <https://www.rfc-editor.org/rfc/rfc6890>
* **RFC 4291 §2.5.2 and §2.5.3** for the unspecified and loopback addresses, and
  **RFC 5952** for how an address is written down.

## 1. What a program is asking for when it does not name an address

`getaddrinfo` answers the question "where should I listen?" when it is given a
NULL node name and `AI_PASSIVE`. RFC 3493 §6.1: with `AI_PASSIVE` and a NULL
`nodename`, the returned socket addresses are "suitable for binding a socket
that will accept connections", and the address is "the wildcard address". The
family of that wildcard comes from `ai_family`, not from the flag.

Measured on this machine, asking for port 6379 and printing every answer in the
order the library returned it:

```
hints.ai_family        flags                      answers, in order
AF_UNSPEC              AI_PASSIVE                 0.0.0.0   ::
AF_INET6               AI_PASSIVE                 ::
AF_INET                AI_PASSIVE                 0.0.0.0
AF_UNSPEC              (none)                     ::1       127.0.0.1
AF_INET6               AI_PASSIVE|AI_V4MAPPED     ::
AF_INET6               AI_PASSIVE|AI_V4MAPPED|AI_ALL   ::
```

Four things to take from it:

* `AF_UNSPEC` with `AI_PASSIVE` returns **two** wildcards, and on this machine
  it returns the IPv4 one **first**. "No family named" is not "IPv6"; it is
  "every family the host has, in the library's order".
* Which family comes first is not a constant. It is the RFC 6724 ordering
  applied to two wildcards, and `/etc/gai.conf` — empty on this machine, so the
  built-in table applies — can change it. A program that wants a particular
  family has to say so.
* Dropping `AI_PASSIVE` changes the answer entirely: a NULL node without the
  flag means *loopback*, not the wildcard. The two are not interchangeable.
* `AI_V4MAPPED` and `AI_ALL` do nothing here. They widen what a **name** or a
  literal resolves to; they do not add a second wildcard.

The same call with a node name is unaffected by any of this: `localhost` returns
`::1` and `127.0.0.1` whatever `AI_PASSIVE` says, because the addresses come
from the name service.

## 2. An IPv6 wildcard socket, and who decides whether it carries IPv4

An `AF_INET6` socket bound to `::` may or may not also accept IPv4 connections,
as mapped addresses. RFC 3493 §5.3 makes that a per-socket option,
`IPV6_V6ONLY`, and leaves the default to the implementation. On Linux the
default comes from `net.ipv6.bindv6only`.

Measured here:

```
$ cat /proc/sys/net/ipv6/bindv6only
0
```

So on this machine an `AF_INET6` socket that says nothing **does** accept IPv4,
and the only thing standing between `bind(::)` and an IPv4 peer is an explicit
`setsockopt(IPV6_V6ONLY, 1)`. A listener that is "IPv6" because its `ai_family`
was `AF_INET6` is not thereby a listener that refuses IPv4 — those are two
separate facts about the same socket, and only one of them is decided by
`getaddrinfo`.

This is also what makes two wildcards on one port possible at all: `::` with
`IPV6_V6ONLY` set and `0.0.0.0` can be held simultaneously by two different
processes, and each answers only for its own family. Without the option the
first one owns the port for both.

## 3. What this machine has

```
$ ip -o -6 addr
1: lo    inet6 ::1/128 scope host
$ ip -o -4 addr
1: lo    inet 127.0.0.1/8 scope host
2: eth0  inet 172.17.0.2/16 brd 172.17.255.255 scope global
```

`lo` carries `::1` and nothing else; `eth0` has **no IPv6 address at all**.
`CAP_NET_ADMIN` is not held, so none can be added.

A socket bound to the IPv6 wildcard therefore binds successfully and is
reachable at `::1`, and there is no global IPv6 address on this host to reach it
at. A socket bound to the IPv4 wildcard is reachable at `127.0.0.1` and at
`172.17.0.2`. Anything that has to be shown about which families a listener
serves is shown on loopback.

## 4. Which addresses are meant to be written down

RFC 5737 §1 is explicit about the problem: addresses used in documentation get
copied into running configurations, and an address that belongs to somebody gets
traffic it did not ask for. So three IPv4 blocks and one IPv6 block exist for
the purpose:

| Purpose | IPv6 | IPv4 | Defined in |
|---|---|---|---|
| Documentation, examples | `2001:db8::/32` | `192.0.2.0/24`, `198.51.100.0/24`, `203.0.113.0/24` | RFC 3849, RFC 5737 |
| This host, always | `::1` | `127.0.0.0/8` | RFC 4291 §2.5.3, RFC 6890 |
| No host | `::` | `0.0.0.0/8` | RFC 4291 §2.5.2, RFC 6890 |

`192.168.0.0/16` is **not** one of them: RFC 1918 reserves it for private
networks, which means somebody's machine really is at `192.168.1.1`, and RFC
5737 §1 names exactly this as the practice it is replacing.

Two conventions that follow, and that RFC 5952 §4 and §5 back up:

* An address written down in prose or in a usage message is written in the same
  canonical form the program prints — lower case, `::` at the longest run of
  zeroes, and with a port attached as `[address]:port`.
* An IPv4-mapped address is written in the mixed form of RFC 5952 §5,
  `::ffff:127.0.0.1`. It is an IPv6 address that contains four decimal numbers;
  it is not an IPv4 address written in a document.

## 5. Where this repository documents itself

The material a reader of this repository actually reads, in the order they are
likely to meet it:

* `README.md` at the root, and `deps/hiredis/README.md` for the vendored client.
* The annotated `redis.conf` and `sentinel.conf`, which document every directive
  an operator can set, in prose, beside the directive.
* The usage text each program prints for `--help` or for a bad invocation:
  `usage()` in `src/redis.c`, `usage()` in `src/redis-benchmark.c`, `usage()` in
  `src/redis-cli.c`, and the `--help` of the helper scripts under `utils/`.
* `deps/hiredis/examples/`, which are complete programs a reader compiles and
  runs.
* Comments in `src/` that quote the *shape* of something the program produces —
  a log line, a `CLIENT LIST` row, an `INFO` field, a `-MOVED` redirect, the key
  of a cluster node table. These are read as a specification of a format by
  anyone changing the code that produces it.
* `src/anet.h` and `src/anet.c`, where the address-class constants are defined,
  each next to the prefixes that define the class in both families.

## 6. Measuring throughput on a machine you do not own

`redis-benchmark` reports requests per second for each test it runs. On a shared
and unpinned machine the absolute figure is not a property of the tree: it moves
with whatever else is running, and a run that is twice as fast as another run of
the same binary is ordinary.

What is stable enough to mean something is a **ratio measured in the same run**:
the same tree, the same benchmark, the same host, the same minute, over two
different transports. Taking the best of several repetitions of each side before
dividing removes most of what a neighbouring process does to a single run, since
a scheduler steals time but does not give it back.

`redis-benchmark`'s own flags for this: `-n` for the number of requests, `-c`
for the concurrency, `-t` to select tests, `-q` for one line of output per test,
and `--csv` for a machine-readable form.
