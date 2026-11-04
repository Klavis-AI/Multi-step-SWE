# Connection establishment to a multi-address name — the reference for this workspace

This note collects the parts of the published standards that the work in this
workspace rests on. It states what the standards say; it does not state what
this repository must do. The requirements are in the step instruction.

The documents are public. Look them up in full if you want the exact wording:

* **RFC 8305 — Happy Eyeballs Version 2: Better Connectivity Using Concurrency.**
  <https://www.rfc-editor.org/rfc/rfc8305>
* **RFC 6724 — Default Address Selection for Internet Protocol Version 6 (IPv6).**
  <https://www.rfc-editor.org/rfc/rfc6724> (it obsoletes RFC 3484, which some
  C library documentation still cites by that number)
* **RFC 3493 — Basic Socket Interface Extensions for IPv6**, §6.1 for
  `AF_UNSPEC`. <https://www.rfc-editor.org/rfc/rfc3493>
* **POSIX.1-2017**, the `connect`, `select`, `poll` and `getsockopt` pages.
  <https://pubs.opengroup.org/onlinepubs/9699919799/>
* The C library's own manual pages, `man 2 connect`, `man 2 getsockopt` and
  `man 3 getaddrinfo`, which are installed in this container.

## 1. What problem RFC 8305 is about (§1)

A name can carry addresses of both families. If a host has an IPv6 address
configured but no working IPv6 path to that destination, a client that tries the
IPv6 address alone, or tries it first and waits for it to fail, is delayed by
however long the network takes to give up on it — which, when packets are
silently discarded rather than refused, is the client's own connect timeout.
RFC 8305 §1 states the aim: the client should still connect "as fast as an
IPv4-only client" while continuing to prefer IPv6 when IPv6 works.

RFC 8305 is an update of RFC 6555; version 2 is the one with the sorting and
delay rules below.

## 2. Resolution (§3, §5 of RFC 3493)

RFC 8305 §3 describes the whole procedure as resolution, then sorting, then
attempts. For resolution it assumes queries for both families, and notes that an
answer for one family may arrive before the other.

`getaddrinfo()` with `ai_family = AF_UNSPEC` is the interface POSIX and RFC 3493
§6.1 provide for that: it asks for every family the caller will accept and
returns a **list**. A `struct addrinfo` in that list carries everything needed to
make one attempt — `ai_family`, `ai_socktype`, `ai_protocol`, `ai_addr`,
`ai_addrlen` — so the caller does not have to name a family anywhere.

## 3. Sorting the addresses (RFC 8305 §4, RFC 6724)

Two separate orderings, applied one after the other.

**First, destination address selection.** RFC 8305 §4 says the client "SHOULD"
sort the resolved addresses by the rules of RFC 6724 §6 — the preference order
that considers scope, deprecation, label matching and prefix length against the
source addresses the host has. On a POSIX system with a conforming C library
this has already happened by the time `getaddrinfo()` returns: `man 3
getaddrinfo` on glibc states that the list it returns is sorted according to
RFC 3484 (RFC 6724's predecessor). So the order in the returned list *is* the
sorted order, and re-sorting it is not part of the algorithm.

**Second, family interleaving.** RFC 8305 §4 then reorders that sorted list so
that the families alternate: the first address of one family, the first address
of the other family, the second address of the first family, and so on.
Section 4 states that the first address "SHOULD" be IPv6, and that if one family
runs out, the remaining addresses of the other family follow in their sorted
order.

Worked through, a sorted list of

```
v6-a, v6-b, v4-a, v4-b, v4-c
```

interleaves to

```
v6-a, v4-a, v6-b, v4-b, v4-c
```

— relative order within each family preserved, no address dropped, none added.
RFC 8305 §4 also allows a client to limit how many addresses of one family it
places before the first of the other; the plain alternation above is the
description the section leads with.

## 4. Making the attempts (RFC 8305 §5)

The list from §4 is attempted in order, with concurrency bounded by a delay:

* the **first** attempt starts immediately;
* each **subsequent** attempt starts either when the Connection Attempt Delay
  has elapsed since the previous attempt was started, **or** as soon as the
  previous attempt fails — RFC 8305 §5 says a failure means the next attempt
  "SHOULD" start immediately rather than waiting out the remaining delay;
* the **Connection Attempt Delay** is a timer §5 defines: its RECOMMENDED value
  is **250 milliseconds**, it MUST NOT be less than 10 milliseconds, and it MUST
  NOT be greater than 2 seconds. §5 notes the value is a trade-off between how
  long a user waits and how much redundant traffic is generated;
* the **first** attempt to complete its handshake is the connection that is
  used; §5 says all other attempts in flight are then cancelled.

RFC 8305 §5 is explicit that this is concurrency and not a faster serial walk:
more than one attempt may be outstanding at the same time, which is what bounds
the total time by the delay rather than by any one address's timeout.

The section also observes that the whole procedure only ever *adds* attempts.
If the name has a single address, the first attempt is made immediately and
there is never a second one to delay.

## 5. Attempting a connection without blocking on it (POSIX)

Racing attempts requires that starting one does not block the caller inside it.
POSIX and `man 2 connect` describe the mechanism:

* on a socket whose `O_NONBLOCK` file status flag is set, `connect()` returns
  `-1` with `errno` set to `EINPROGRESS` and the attempt proceeds in the
  background;
* the socket becomes **writable** (`select()`, `poll()`, or any equivalent) when
  the attempt has finished, whether it succeeded or failed;
* whether it succeeded is then read with `getsockopt(fd, SOL_SOCKET, SO_ERROR,
  …)`: zero means connected, and any other value is the `errno` the attempt
  failed with. `man 2 connect` states that this is how the error is retrieved
  for a non-blocking connect;
* `connect()` may also return `0` immediately, which means the connection was
  established without waiting.

Closing a socket whose attempt is still in progress abandons that attempt.

## 6. Where this repository's own interface is documented

The behaviour of the programs in this workspace — the configuration directives
they read, the commands they answer and the way they are built — is documented
by the repository itself: `README.md`, the annotated `redis.conf` and
`sentinel.conf`, and the test suite under `tests/`. Those files are the
authority on what this tree already does.
