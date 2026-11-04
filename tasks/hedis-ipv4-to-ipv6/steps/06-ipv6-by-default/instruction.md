# Step 6 — IPv6 is what this tree does; IPv4 is what an operator asks for

## 1. Implement this milestone

Every mechanism this migration needs is already in place. The tree looks
a host up without naming a family, races the addresses it gets back, writes and
parses every address in one canonical bracketed form, compares addresses by
value, classifies them by prefix, and runs on a kernel where `AF_INET` sockets
cannot be created at all. Nothing is missing.

What is still true is that none of it is the *default*. Start the server the way
this repository ships it and you get IPv4: `redis.conf` says `bind localhost`,
`localhost` has an address in each family, and two listeners come up. Delete the
`bind` line and you get IPv4 anyway, because the wildcard is resolved with no
family named and the host answers with both. A reader who opens
`redis-benchmark --help` is told to try `192.168.1.1`; a reader who opens the
vendored client's `README.md` is shown `redisConnect("127.0.0.1", 6379)`; a
reader of `networking.c` is told that `CLIENT LIST` prints `127.0.0.1:1234`,
which it has not printed since this repository settled its address text
(`DOCS/03-address-text.md`). The machinery migrated and the defaults, the
documentation and the examples did not.

**A server that was not told what to bind listens on IPv6 and creates no IPv4
socket; IPv4 happens where, and only where, an operator wrote an IPv4 address;
and no file this repository ships still offers an IPv4 address as an example of
what this tree does.**

### The default is IPv6

A server started with **no `bind` directive** listens on the IPv6 wildcard and
on nothing else. It creates no `AF_INET` socket, so an IPv4 client is refused
rather than served, and a second process may hold the IPv4 wildcard on the same
port while it runs.

The configuration this repository ships comes up that way: the `bind` line in
`redis.conf` is no longer active, and the annotation around it says what a
server with no `bind` now listens on and what to write to get a different
family. `sentinel.conf`, which never had a `bind` line, inherits the same
default.

Where this machine has no IPv6 address to bind and the operator did not say what
to bind, the server **fails and says so**, exactly as it already does for any
other address it was told to bind and could not: the line it logs names the
address it could not bind — the IPv6 wildcard, written and joined to the port
the way this tree writes every address — and the reason it could not. The
default is a choice of family, not a preference to fall back from.

### An explicit IPv4 address still means IPv4

This step removes IPv4 from the one place the tree was still choosing it for the
operator. It removes it from nowhere else, and it deprecates nothing:

* `bind 127.0.0.1`, `bind 0.0.0.0`, `bind 127.0.0.1 ::1` bind exactly what they
  name, on a machine that has it, and fail the same way as before on one that
  does not. No warning, no refusal, no rewriting of what was asked for.
* A **name** in `bind` is still resolved without naming a family and still gets
  a listener for every address that resolution returned — including its IPv4
  addresses. That is the family-independent lookup
  (`DOCS/01-address-family-independence.md`), and it is untouched here. The
  default is not a name: it is the absence of the directive.
* `redis-cli -h 127.0.0.1`, `redis-benchmark -h 127.0.0.1`,
  `--slaveof 127.0.0.1 6379`, `CLUSTER MEET 127.0.0.1 6379` and
  `SENTINEL MONITOR … 127.0.0.1 …` all behave as they do today.

### No IPv4 address is left standing as an example

Every file this repository ships is read by somebody. A usage message, a comment
that gives the shape of a format, a `README.md`, an annotated configuration
file, a vendored client's example program — each of them tells a reader what
this tree does, and each of them is wrong wherever it answers with an IPv4
address.

The material in question is this project's own: everything under `src/` that is
not the bundled Lua library in `src/lua/`, everything under `tests/` and
`utils/`, the configuration files and documents at the root, and the vendored
Redis client in `deps/hiredis/` — which is network code this project distributes
and whose examples tell a reader how to connect. The bundled language runtime
and allocator in `deps/lua/`, `deps/jemalloc/` and `deps/linenoise/` are not
this project's description of itself and are not touched; the standing
requirement that no IPv4-only type or function survive anywhere
(`DOCS/01-address-family-independence.md`) already covers what matters about
them, and it still holds.

The rule is about the **role** the address plays, and there is exactly one role
an IPv4 address in dotted-quad form may still play anywhere in this repository:

> the **definition of an address class** the tree must still recognise, written
> as a prefix with a length — `127.0.0.0/8` and `224.0.0.0/4` are two of them.

The form is the rule and the two above are only examples of it: a dotted quad
immediately followed by `/` and a prefix length is a specification of a class,
and there are more of them in this tree than those two.

They are the IPv4 halves of the address classification this workspace already
carries (`DOCS/05-address-classes.md`), and each is the specification of a class
rather than an example of an address. Every other
dotted quad this repository ships is an example of what the tree does, and it
becomes the address the tree would actually use: the usage text of every shipped
program, the comments that quote the format of a log line, of a `CLIENT LIST`
row, of an `INFO` field, of a `-MOVED` redirect or of a cluster address, the
project `README.md`, the annotated `redis.conf` and `sentinel.conf`, and the
vendored client's `README.md` and `examples/`.

Two things that are not violations, so that this is not read more widely than it
is meant:

* An **IPv4-mapped address written in RFC 5952 §5 mixed form** —
  `::ffff:127.0.0.1` — is an IPv6 address. The tree already prints it that way,
  and a comment has to be able to say so.
* An RFC section number is not an address. `RFC 4291 section 2.5.5.2` stays as
  it is. Like a prefix, it is known by its form: the word `section`, or the
  sign `§`, directly in front of the number is what says it is one. Four
  numbers between dots with nothing in front of them saying what they are
  read as an address.

An address is not made to disappear by deleting the sentence that contained it.
The usage text still documents every option, the comments still say what format
they were saying, `README.md` still says what it said, and the examples still
compile and still connect — to an address this tree would use.

### Serving over IPv6 is not slower

The point of the default is that the normal path is the IPv6 path, so the IPv6
path may not be the slow one. Measured on the same host in the same run, with
this same tree: the throughput `redis-benchmark` reports against a server this
step's default brought up must be **within a factor of two** of what it reports
against the same tree bound to an explicitly-configured IPv4 address. This is a
guard against an implementation that pays for the default per connection — a
resolution on every accept, a conversion on every packet, a linear scan of a
list of families. It is not a benchmark, and the band is wide because the
machine is shared.

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
* **Everything this workspace already established**, with `DOCS/` as its
  references: family-independent lookup, the RFC 8305 race and its timing, canonical
  text and bracketed `[addr]:port` parsing, running with no IPv4 in the kernel,
  classification by prefix, peers as unicast addresses, mapped addresses as the
  address they embed. This step changes a default and a pile of prose; it changes no
  mechanism any of those rest on.
* **Unix sockets** are untouched: `unixsocket` still works, and a server with
  `unixsocket` and no `bind` still gets the TCP listener this step describes.
* **`port 0`** still means no TCP listener at all.
* The repository's own test suite is not weakened, renamed, filtered or
  disabled. Where an asset or a helper of the suite has to move because the
  default moved, it is corrected in place; what the tests *check* is not
  touched.
* No new dependency, no new package and no new file in the tree are needed for
  any of this.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/06-ipv6-by-default.md` — what "the default"
is in a `getaddrinfo`-based listener, what `AI_PASSIVE` does and does not do,
what the host's own configuration contributes, which RFCs reserve the addresses
that documentation and examples are supposed to use, and what was measured about
all of that on this machine. It records what the standards and this platform
say, not what this repository must do.

`/app/DOCS/01-address-family-independence.md`,
`/app/DOCS/02-connection-establishment.md`,
`/app/DOCS/03-address-text.md`,
`/app/DOCS/04-operating-without-ipv4.md` and
`/app/DOCS/05-address-classes.md` are the references for what this tree
already does, and everything they cover still holds.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` for the layout of this
workspace, the commands that build and run the tree, and what to leave behind.
`/app/README.md`, the annotated `/app/redis.conf` and `/app/sentinel.conf`, and
the suite under `/app/tests/` are the project's own documentation of how it is
built, configured and exercised — and three of those four are also material this
step has to correct.

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
