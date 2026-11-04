# Step 4 — Run on a host that has no IPv4 at all

## 1. Implement this milestone

The tree now looks a host up without naming a family, races the addresses that
lookup returns, and writes every address it prints in one canonical form. All of
that still assumes the kernel underneath will hand out an IPv4 socket when
something asks for one. On a host that implements no IPv4 — `socket(AF_INET, …)`
fails with `EAFNOSUPPORT`, every other family untouched — this repository does
not run. The server stops before it has listened on the addresses that do work,
because one address of a family this kernel does not have was fatal to the whole
call. And once it is past that, everything it ships still points at `127.0.0.1`:
the configuration file an operator starts from, the asset the test suite hands
every server it launches, the cluster helpers, the vendored client's own
Makefile.

**On a host with no IPv4, this repository builds, starts on the configuration
file it ships, serves clients, replicates, forms and reshards a cluster, and
runs Sentinel through a failover — and nothing it ships names an IPv4 address as
a place to go. IPv4 stays available to an operator who asks for it by name.**

### A family the kernel does not implement is not a configuration error

A `bind` line names hosts, and a host is resolved before anything is bound. The
list that comes back is the resolver's, and the resolver answers out of the
host's name service, which knows nothing about the running kernel: a name with
an address in each family resolves the same on a machine with no IPv4 stack at
all. So the list can contain an address for which no socket can exist, and no
configuration file can be written that avoids it.

* When a socket cannot be created **because the family is not implemented**,
  that address is skipped and the remaining addresses are listened on. A server
  configured for a host that has both an IPv6 and an IPv4 address comes up on
  such a machine, listening on the IPv6 one.
* When **nothing** could be listened on, the call fails as it does today and
  says why.
* Every other failure keeps the meaning it has: an address that belongs to no
  interface on this host, a port already in use, a permission refused — those
  are configuration errors, they are still fatal, and they still say what went
  wrong. The distinction is between an address this machine cannot have and an
  address this machine has a problem with.
* The connecting side already walks its list and moves past an address it cannot
  use. It keeps doing so, for this failure as for a refused connection.

### Nothing this repository ships names an IPv4 address as a place it reaches

Every file in this tree that something *acts on* is in scope: the annotated
`redis.conf` and `sentinel.conf`, the assets under `tests/`, the Tcl suite's own
helpers and instance definitions, the test files themselves, the scripts and
helpers under `utils/`, and the Makefiles of the vendored dependencies. No
dotted-quad IPv4 address survives in any of them.

* **A commented-out line is a place the repository goes.** An operator reads the
  annotated configuration and copies the example under the paragraph that
  explains it; a directive commented out in an asset is a directive somebody
  uncomments. These count exactly as the live ones do.
* **Deleting is not correcting.** Removing a helper's address assignment, or the
  asset directive that sets one, leaves the tree with a different default rather
  than a working one — usually the wildcard — and loses whatever the line was
  there to pin down. The files that carry these addresses today still exist and
  still do their job afterwards.
* Where an address is wanted as an **example** rather than as a destination, the
  documentation prefix `2001:db8::/32` (RFC 3849) is what examples are written
  from.
* Prose an operator reads is not in scope: `README.md`, the manual pages, and
  the usage text compiled into the programs are descriptions of the software,
  not places it goes. That includes the example command lines those programs
  print, which stay exactly as they are. The one thing in that text this step
  does move is the compiled-in default host itself, because a program that
  prints `(default: …)` prints what it is going to do.

### The shipped tools reach something by default

`redis-cli` and `redis-benchmark` both have a compiled-in default host, used
when no `-h` is given. On a host with no IPv4 that default is reached like any
other target, so it becomes the name `localhost` — a host this tree can still
connect to there, and the same machine it names today.

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
* **IPv4 on request still works.** An operator who configures `bind 127.0.0.1`,
  or passes `-h 127.0.0.1`, gets IPv4 on a machine that has it, exactly as
  today. This step removes IPv4 from the places the tree chose it for the
  operator, not from the places the operator chooses it.
* On a machine without IPv4, that same explicit request **fails and says so**. A
  server told to bind an address it cannot bind does not come up somewhere else
  instead.
* A server given **no `bind` directive** still listens on the wildcard, in every
  family this machine has — and an IPv6 wildcard listener still refuses IPv4
  connections rather than accepting them through mapped addresses, as it does
  today.
* The repository's own test suite is not weakened, renamed, filtered or
  disabled. Its assets and helpers are corrected in place; the tests themselves
  are the ones that were there. The handoff in your workspace says not to edit
  the tests: that still holds for what they *check*. The addresses their assets,
  their helpers and the test files themselves use are not what they check, and
  those are exactly what moves here.
* How addresses are **chosen** and **written** is settled: the family-independent
  lookup, the RFC 8305 race with its timing, and the canonical text and
  bracketed joins already in this tree are untouched.
* Which spelling the shipped configuration's own `bind` line ends up with —
  `::1`, `localhost`, or another name — is not decided here. What it may not be
  is an address of a family the machine might not have.
* No new dependency, no new package, and no new file in the tree are needed for
  any of this.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/04-operating-without-ipv4.md` — what "no
IPv4" is in system-call terms, what POSIX says `socket()` fails with and when,
RFC 4038 §3.2 and RFC 3493 §6.1 on asking the resolver for every address and
trying all of them, BCP 177 on IPv4 not being required for complete function,
and a self-contained program that reproduces such a host here so the work can be
tried rather than reasoned about. It states what the standards say and what this
container does, not what this repository must do.

`/app/DOCS/01-address-family-independence.md`,
`/app/DOCS/02-connection-establishment.md` and
`/app/DOCS/03-address-text.md` are the references for what this tree already
does, and everything they cover still holds: one lookup that names no
family, the RFC 8305 attempt algorithm, canonical text, bracketed joins, and
addresses compared by value.

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
