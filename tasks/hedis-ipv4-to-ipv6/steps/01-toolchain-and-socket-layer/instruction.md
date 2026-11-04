# Step 1 — Build on the current toolchain, and take the address family from the resolver

## 1. Implement this milestone

This tree was written when a socket was an IPv4 socket unless it was told
otherwise, and it is pinned to a compiler and a C library that have since moved
on. Two things must become true, and they are one piece of work: the tree
**builds and runs on the toolchain in this container**, and **nothing in it
decides an address family when it is written** — the family comes from the
address the resolver returned.

### The build

* `make` in `/app`, from a clean tree, completes successfully with the compiler
  and C library the container provides, and leaves all six programs in `src/`:
  `redis-server`, `redis-cli`, `redis-sentinel`, `redis-benchmark`,
  `redis-check-rdb` and `redis-check-aof`.
* The build reports no error, and no call to a function whose declaration the
  compiler never saw. The second matters as much as the first here: a call to a
  resolver function with no declaration in scope compiles, links, and then
  truncates a returned pointer at run time.
* The build keeps honouring the standard `CFLAGS` variable given on the `make`
  command line — a flag passed that way still reaches every compilation, in
  `src/` and in `deps/`, exactly as the shipped `Makefile` arranges today.
* Nothing is silenced instead of fixed. A diagnostic that has been switched off
  is still a diagnostic: do not add a warning-disabling compiler flag, do not
  add a pragma telling the compiler to ignore a diagnostic, do not comment out
  or preprocess away code to stop a message, and do not add a flag that restores
  a superseded default of an older compiler so that code the current one rejects
  is accepted again. Fix what the compiler is objecting to.

### No IPv4-only construct survives

This applies to every file the repository ships — `src/`, all of `deps/`,
`utils/`, `tests/`, the scripts and the configuration files, and sources that
only compile on other platforms. Nothing is carved out; the vendored trees
under `deps/` are in scope like everything else, and since none of them opens a
socket there is nothing in them to change:

* No IPv4 address family constant — spelled `AF_INET`, spelled `PF_INET`, or
  written as the integer either of them expands to — reaches a call that creates
  a socket, is stored into the address-family field of a socket address or of a
  resolver result, or is compared against one.
* No IPv4-only address type is used: no `struct sockaddr_in`, no
  `struct in_addr`, and none of their members. An address whose family is not
  the code's own decision is held in a `struct sockaddr_storage` and passed on
  as a `struct sockaddr *` with the length that came with it.
* None of the IPv4-only C library entry points is called, and none is left in
  any of the six programs' dynamic imports: the `gethostbyname`/`gethostbyaddr`
  family, `inet_addr`, `inet_aton`, `inet_ntoa`, `inet_network`,
  `inet_makeaddr`, and the rest of the interface that predates `getaddrinfo`.
* No IPv4-only constant naming an address or a protocol appears either: the
  `INADDR_*` wildcard, loopback, broadcast and error values, and the IPv4-in-IPv4
  protocol number.

Three families may still be named, because naming them is not a decision about
IPv4: `AF_UNSPEC`, which is how a caller declines to choose; `AF_UNIX`, for the
Unix-domain socket the server already offers; and `AF_INET6`, where the code is
acting on an address it has already been given and which is IPv6.

### The family comes from a resolved address

* Every lookup of a host — a name or a literal — is made without asking for a
  family, and its result is used as it was returned: a list of addresses, each
  carrying the family, socket type, protocol, address and address length needed
  to use it, in the order the resolver produced them.
* Every socket the tree creates for an internet address is created from those
  fields of one of those results.
* No conversion between an address and its textual form is told which family to
  use. Rendering an address as text, and parsing text back into an address, both
  take the family from the address itself, so there is one code path for every
  family the kernel can produce rather than one path per family somebody
  remembered to write.
* The Unix-domain socket is not an internet address path; it is unchanged.

### Listening

* Every entry of the `bind` configuration directive is resolved, and the server
  opens a listening socket for **every** address that resolution returns. One
  entry that resolves to several addresses produces several listening sockets;
  several entries add up; a literal address resolves to itself and produces one.
  Nothing deduplicates the result: an address that two entries both resolve to
  is bound twice, and the second of those two is a bind of an address and port
  the server already holds. That second bind fails, and nothing is done to the
  listening sockets to make it succeed — writing `bind ::1 ::1` refuses to start
  on this tree today, and a `bind` line whose entries overlap refuses to start
  for the same reason.
* A configured address the server cannot listen on is a startup failure,
  exactly as an unbindable literal address is today — every address resolution
  returned has to work, and the server does not come up having quietly kept
  the subset that did.
* With no `bind` entry configured at all, the server listens on the wildcard
  address of every family the host supports.
* Every IPv6 listening socket is restricted to IPv6 before it is bound, so it
  never accepts an IPv4 connection. An IPv6 wildcard listener and an IPv4
  wildcard listener can therefore hold the same port on this host at the same
  time, each answering only for its own family.

### Connecting

* Every outbound TCP connection the repository makes — both command-line tools,
  the bundled client library under `deps/hiredis/`, the link a replica opens to
  its master, and the cluster bus — resolves the host it was given without
  asking for a family, and creates its socket from one of the addresses that
  came back, taking the family, the socket type and the protocol from that
  address rather than from the code.
* A caller that waits for its connection attempts those addresses in the order
  the resolver returned them, until one of them connects: a refusal is a reason
  to try the next address, not a reason to stop. A connection made on a later
  address is a success and nothing but a success — the failures walked past are
  not left anywhere the caller can read them back and conclude it failed
  anyway. If none of them connects, the failure reported is the last one's.
* A caller that does *not* wait — one that starts the connection and lets its
  event loop finish it — cannot walk a list without becoming a caller that
  waits, so it takes the first address it can start a connection on and reports
  a failure the way it reports one today. Which callers those are does not
  change here: the interactive client waits for its connection, and the load
  generator, the link a replica opens to its master and the cluster bus do not.
* A host that has an address of each family is therefore reached whichever of
  the two a server happens to be listening on, by every caller that waits. A
  host whose only address is of a family nothing is listening on fails for all
  of them, as it does today.

### Everything else is unchanged

This step changes which addresses the tree creates, listens on and reaches. It
changes nothing else: every command and every reply, every log line and `INFO`
field, the way an address is spelled in any text the tree writes, the
configuration directives it accepts and their defaults, the Unix-domain socket,
the shipped configuration files, and the repository's own test suite all stay as
they are.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/01-address-family-independence.md` — the
family-independent resolution interface, `struct sockaddr_storage`, the IPv6
wildcard and loopback addresses, and the `IPV6_V6ONLY` socket option, each with
the RFC and POSIX section it comes from.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` for the layout of this
workspace, the commands that build and run the tree, and what to leave behind.
`/app/README.md`, `/app/INSTALL`, the annotated `/app/redis.conf` and
`/app/sentinel.conf`, and the suite under `/app/tests/` are the project's own
documentation of how it is built, configured and exercised.

## 3. Meta requirements

* The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
* Work in the cumulative `/app` workspace and preserve the supplied source,
  configuration files, documents and handoff file. Do not write a prose answer
  or add an alternate entry point in place of the work above.
* Do not cheat by using online solutions or hints specific to this task.
* Do not weaken, filter, rename or disable the repository's own tests.
* Build the tree and run the repository's own suite before you finish, and leave
  `/app/WORKSPACE_HANDOFF.md` accurate.
