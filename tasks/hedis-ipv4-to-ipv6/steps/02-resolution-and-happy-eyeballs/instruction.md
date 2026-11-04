# Step 2 — Reach a name by racing its addresses, not by walking them

## 1. Implement this milestone

The tree now resolves a host without naming a family and attempts the addresses
it gets back until one of them connects. That is enough when an address that
cannot be reached says so: a refusal comes back in microseconds and the next
address is attempted at once. It is not enough when an address answers
**nothing at all** — no acceptance, no refusal, silence. Attempting such an
address and waiting for it to finish costs the operating system's own connect
timeout, and every address behind it waits that long too. A host that is
reachable over one family and blackholed over the other is then unreachable in
practice, which is the failure a dual-stack client exists to avoid.

**Every outbound TCP connection this repository makes must establish itself by
the algorithm of RFC 8305 (Happy Eyeballs Version 2).** The whole of the change
is in how attempts are ordered and overlapped. *Which* addresses are used is
settled already — one lookup that names no family, every address it returns
available to attempt — and the spelling of every address the tree
writes stays exactly as it is.

### The order the addresses are attempted in

* The addresses come from one family-independent lookup of the host, as they do
  today. The resolver has already put them in its own preference order; that
  order is **kept within each family**.
* The two families are then **interleaved**, as RFC 8305 section 4 describes:
  the first address of one family, the first of the other, the second of the
  first, and so on. **The first address attempted is an IPv6 address** whenever
  the host has one. When one family runs out, the remaining addresses of the
  other follow in their order.
* No address the resolver returned is dropped from the list, and none is added
  to it. A host with addresses of only one family is attempted in exactly the
  order the resolver gave.

### When each attempt starts

* The first attempt starts immediately.
* Each later attempt starts when a **Connection Attempt Delay** has passed since
  the previous attempt was started — **or sooner: as soon as an attempt fails,
  the next address is attempted at once**, because the delay is how long an
  attempt that is *still running* postpones its successor and not a toll charged
  per address. A list of addresses that all fail instantly is therefore still
  walked in a moment, and a failure in the middle of a list costs nothing.
* The Connection Attempt Delay is the value RFC 8305 section 5 recommends:
  **250 milliseconds**.
* Attempts that have not finished keep running while later ones start, so more
  than one attempt is outstanding at a time. An attempt is never abandoned
  merely because the next one has started.

### How the race ends

* The **first attempt to complete its connection is the one that is used**. A
  later address that connects while an earlier attempt is still running wins,
  and the caller does not wait for that earlier attempt to reach any conclusion.
* The connection handed back is in the state its caller asked for and is usable
  for the caller's next operation. In particular the failure of an attempt the
  race walked past is not left anywhere the caller reads back as its own
  result — as already today, a connection made on a later address is a success and
  nothing but a success.
* If every attempt fails, the failure reported is that of the attempt **furthest
  down the attempt order**, which is now the interleaved order rather than the
  resolver's.

### A preferred source address is now a per-attempt matter

A caller may name a **source address** to bind its socket to before connecting —
the link a replica opens to its master does, from the server's first configured
`bind` entry. A source address has a family, and the destinations of a race no
longer all share it.

* The source address is applied to **each attempt**, not once to the connection
  as a whole.
* Where the caller expresses the source address as a **preference** — the
  existing best-effort form — an attempt whose destination the source address
  cannot be bound for is made **without** it rather than abandoned. Otherwise a
  replica bound to an address of one family could never reach a master over the
  other, which is a whole family of the race lost to a preference.
* That fallback belongs to the bind and to nothing else. Within one race each
  address of the interleaved list is attempted **once**: an attempt whose source
  address *was* bound and whose connection then failed has simply failed, and is
  not made again without it. What happens when the race as a whole fails is
  unchanged — a best-effort source address is dropped and the whole connection
  attempted again without it, exactly as it is today.
* Where the caller expresses it as a **requirement**, an attempt that cannot
  bind it fails, exactly as it does today.

### Callers that do not wait for their connection

Some callers do not wait for the connection they asked for: they start it and
let their event loop finish it. Being handed an unfinished attempt does not
exempt such a caller from the algorithm. Wherever one of them is given a
destination with more than one address to attempt — the load generator is given
a host argument, and the link a replica opens to its master is given the master's
configured host — it races those addresses like any other caller, so a master
whose name's earlier addresses answer nothing must still be linked to. The
constraint is what that race costs the loop it runs in:

* The time such a caller spends inside the call that starts the connection is
  bounded by the race itself. It is **never** the operating system's connect
  timeout, and a server whose replication link is retrying a master it cannot
  reach keeps answering its own clients throughout.
* Where there is only **one** address to attempt — a host with a single address,
  or an address handed over as a literal — there is no race to run and such a
  caller waits for nothing at all: it is handed its attempt in progress the
  moment the attempt is started, exactly as it is today.
* Where there is more than one it waits for the race to have had its chance: it
  is handed an attempt that is still in progress only once **every** address has
  had an attempt started **and** a further Connection Attempt Delay has passed
  with none of them completing. Handing back the first attempt at the moment the
  last one starts would hand back precisely the blackholed attempt this step
  exists to survive, with every attempt that could have rescued it abandoned in
  the same breath.

### What does not change

* **The tree still builds clean.** `make` from a clean tree still succeeds with
  the container's compiler and C library, reports no error and no call to a
  function whose declaration the compiler never saw, and leaves all six programs
  in `src/`; a flag given in `CFLAGS` on the `make` command line still reaches
  every compilation in `src/` and `deps/`; and nothing is silenced instead of
  fixed — no warning-disabling flag or pragma, no code commented out or
  preprocessed away, no flag that restores an older compiler's default.
* **No IPv4-only construct comes back**, anywhere in the repository, `deps/`
  included: no IPv4 family constant (`AF_INET`, `PF_INET`, or the integer either
  expands to) reaches a socket call, an address-family field or a comparison
  with one; no `struct sockaddr_in`, `struct in_addr` or their members; none of
  the IPv4-only C library entry points (`gethostbyname`/`gethostbyaddr` and
  their family, `inet_addr`, `inet_aton`, `inet_ntoa`, `inet_network`,
  `inet_makeaddr`) is called or left in any program's dynamic imports; no
  `INADDR_*` constant or IPv4-in-IPv4 protocol number; and no conversion between
  an address and its text is told which family to use.
* An address that is asked for **by literal** is attempted and nothing else is:
  one address, one attempt, whichever family the literal belongs to. IPv4 is
  still reachable when IPv4 is what was asked for.
* A host with one address behaves exactly as it does today, including when that
  address answers nothing: there is no second address to rescue it and the
  attempt runs to whatever end it would have reached anyway.
* A name with no addresses still fails at the lookup, with the failure it fails
  with today.
* The Unix-domain socket is not an internet address and has no addresses to
  race; that path is untouched.
* Everything outside connection establishment stays as it is: every command and
  reply, every log line and `INFO` field, the spelling of any address in any
  text the tree writes, the configuration directives and their defaults, the
  listening side in full, the shipped configuration files, and the repository's
  own test suite.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/02-connection-establishment.md` — RFC 8305's
sorting and Connection Attempt Delay, RFC 6724 and what the C library has
already done to the resolver's answer before it is returned, `AF_UNSPEC`, and
the POSIX interface for a connection attempt that does not block its caller,
each with the section it comes from. `/app/DOCS/01-address-family-independence.md`
collects the parts of the published socket interface the whole workspace rests
on — the protocol-independent lookup and address types, what the addressing
architecture says about the wildcard and loopback addresses, and `IPV6_V6ONLY` —
each with the standard section it comes from. Like the document above, it states
what the standards say and not what this repository must do.

Two properties the tree already has are relied on throughout the work above, and
must still hold when it is finished: a host is looked up **once, without naming
a family**, and every address that lookup returns is available to be attempted;
and the listening side answers on every address it is configured for, over
either family. This step reorders and overlaps the attempts that follow the
lookup. It does not change what the lookup returns, and it does not touch the
listening side at all.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` for the layout of this
workspace, the commands that build and run the tree, and what to leave behind.
`/app/README.md`, the annotated `/app/redis.conf` and `/app/sentinel.conf`, and
the suite under `/app/tests/` are the project's own documentation of how it is
built, configured and exercised.

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
