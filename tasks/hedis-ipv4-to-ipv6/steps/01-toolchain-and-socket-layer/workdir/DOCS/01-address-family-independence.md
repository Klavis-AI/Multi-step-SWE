# Address-family independence — the reference for this workspace

This note collects the parts of the published socket interface that the work in
this workspace rests on. It states what the standards say; it does not state
what this repository must do. The requirements are in the step instruction.

The documents are public. Look them up in full if you want the exact wording:

* **RFC 3493 — Basic Socket Interface Extensions for IPv6.**
  <https://www.rfc-editor.org/rfc/rfc3493>
* **RFC 4291 — IP Version 6 Addressing Architecture.**
  <https://www.rfc-editor.org/rfc/rfc4291>
* **POSIX.1-2017**, the `getaddrinfo`, `getnameinfo`, `socket`, `bind` and
  `setsockopt` pages. <https://pubs.opengroup.org/onlinepubs/9699919799/>
* The C library's own manual pages, `man 3 getaddrinfo` and `man 7 ipv6`, which
  are installed in this container.

## 1. The protocol-independent interface (RFC 3493 §6, §3.10)

RFC 3493 defines a name-and-service resolution interface that is deliberately
not told which address family the caller wants:

* `getaddrinfo()` takes a `struct addrinfo` hint whose `ai_family` may be
  `AF_UNSPEC`, which RFC 3493 §6.1 defines as "the caller will accept any
  protocol family". It returns a **list**, in the order the resolver and the
  system's address-selection policy put it in; each element carries the
  `ai_family`, `ai_socktype`, `ai_protocol`, `ai_addr` and `ai_addrlen` needed
  to create a socket and connect or bind it, with no further decisions.
* `getnameinfo()` is the inverse: given a socket address and its length, it
  writes the numeric host and service strings. It is not given a family — it
  reads the one in the address it was handed, so a single call covers every
  family the kernel can produce.
* Between them, §6 notes, an application "need not have any knowledge of the
  protocol family" it is using. That is the property the interface exists for,
  and it is why RFC 3493 §6.4 recommends both over the older, family-specific
  name and address calls.

RFC 3493 §3.10 defines `struct sockaddr_storage`: an address container large
enough and aligned well enough for any family the system supports, so that code
which holds an address it has not inspected does not have to name a family to
declare a variable.

## 2. Wildcards, loopback, and what a name resolves to (RFC 4291 §2.4, §2.5)

RFC 4291 §2.5.2 defines the unspecified address `::` and §2.5.3 the loopback
address `::1`. `::` in a `bind()` is the IPv6 wildcard, the counterpart of the
IPv4 all-zeroes wildcard; `::1` is the address of the local host itself.

A host name in the system's name service can carry addresses of both families
at once. Resolving such a name yields several results; which of them a program
uses, and how many, is the program's choice, not the resolver's.

## 3. `IPV6_V6ONLY` (RFC 3493 §5.3)

An `AF_INET6` socket may, at the system's discretion, also carry IPv4 traffic
through IPv4-mapped addresses (RFC 4291 §2.5.5.2). RFC 3493 §5.3 defines the
`IPV6_V6ONLY` socket option at level `IPPROTO_IPV6` to settle that explicitly:
set to 1 before `bind()`, the socket is restricted to IPv6 alone.

Two consequences the RFC calls out:

* the default is implementation-defined, so a program that cares must set the
  option rather than rely on the system's default;
* a restricted IPv6 socket does not occupy the port for IPv4, so an IPv6 socket
  and an IPv4 socket can be bound to the same port on the same host, each
  serving its own family. Without the restriction the first of the two owns the
  port for both families and the second `bind()` fails.

## 4. Where this repository's own interface is documented

The behaviour of the programs in this workspace — the configuration directives
they read, the commands they answer and the way they are built — is documented
by the repository itself: `README.md`, the annotated `redis.conf` and
`sentinel.conf`, and the test suite under `tests/`. Those files are the
authority on what this tree already does.
