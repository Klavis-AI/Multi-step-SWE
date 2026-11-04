# Step 5 — Tell addresses apart by what they are

## 1. Implement this milestone

The tree now looks a host up without naming a family, races the addresses that
lookup returns, writes every address it prints in one canonical form, compares
addresses by value, and runs on a machine that has no IPv4 at all. What it still
has no notion of is what an address *is*. Every place it needs to know something
about an address, it matches the text — `"127.0.0.1"`, `"localhost"`, `"::1"` —
and text is not what an address means.

So an address that cannot possibly be another machine is taken for one.
`CLUSTER MEET ff02::1` names a multicast group, which by definition is more than
one interface and is never a place a single node is: the tree answers `OK` and
creates a node for it. `CLUSTER MEET ::` names no interface at all, and is
accepted too. `SLAVEOF` and `SENTINEL MONITOR` take both, and take
`::127.0.0.1` — an IPv4-compatible address, deprecated by RFC 4291 §2.5.5.1,
which is not the IPv4 address it resembles and is not anywhere. And in the other
direction the tree splits one machine in two: `127.0.0.1` and
`::ffff:127.0.0.1` are the same host written two ways, and every table keyed by
address in this repository holds them as two.

**An address is classified by its value and never by its spelling; a peer is
named by a unicast address and nothing else is accepted as one; and an
IPv4-mapped address is the address it embeds, everywhere identity is decided.**

### Classification is by value

Loopback, the unspecified address, link-local unicast, unique-local unicast,
multicast and global unicast are told apart by what the address *is*. An address
this tree has never seen before is classified correctly the first time, which a
list of known spellings cannot do:

* `fe80::1`, `fe80:0:0:0:0:0:0:1` and `fe80::1%eth0` are all link-local;
* `fd00::1` and `fc00::1` are unique-local, and `fec0::1` is not — RFC 3879
  withdrew site-local and RFC 4291 §2.5.7 removed it, so nothing is there;
* `ff01::1`, `ff02::1` and `ff0e::1` are all multicast, whatever their scope;
* `2001:db8::1` is global unicast.

IPv4 addresses are classified on the same terms and from the same kind of
source — the IANA IPv4 Special-Purpose Address Registry — so `127.0.0.0/8` is
loopback, `0.0.0.0` is unspecified, `169.254.0.0/16` is link-local,
`224.0.0.0/4` is multicast, and the RFC 1918 ranges are the local-use ones.

### A peer is somewhere one other machine is

Wherever this repository is handed an address that names **another machine**, an
address that cannot name one is refused, with an error the caller sees, and
nothing is created or changed. The places are `CLUSTER MEET` and the addresses
the cluster learns from gossip, `SLAVEOF`, `SENTINEL MONITOR` and the
`sentinel monitor` directive in Sentinel's configuration file, and the instances
Sentinel learns from a master's `INFO` and from hello messages.

What is refused there:

* a **multicast** address — it names a group, not a host;
* the **unspecified** address — it names no host;
* an RFC 4291 §2.5.5.1 **IPv4-compatible** address, `::a.b.c.d`.

What is *not* refused there: loopback, link-local and unique-local are ordinary
unicast addresses and are perfectly good places for a peer to be — this
repository's own cluster and Sentinel suites run entirely on loopback.

Nor does any of these places change which spellings it takes. `CLUSTER MEET`
and the `SENTINEL MONITOR` command take a numeric address and nothing else, as
they always have — the form `getaddrinfo` accepts with `AI_NUMERICHOST` — and a
host name, a bracketed address or any other text is refused with the error each
already gives. `SLAVEOF` still accepts any host text that is not one of the
addresses refused above, a name included, without looking it up when the
command arrives: it answers `OK`, keeps the text as given, and the name is
resolved only when the replica connects, so one that does not resolve leaves
the link down. A registered name is never judged by its spelling: where a name
is looked up — the `sentinel monitor` directive in the configuration file, for
one — what it denotes is the resolver's answer, and that answer is what is
classified.

A refusal is something the caller is told about. A command answers with an error
that says which address was refused and why. A configuration line that names
such an address is a configuration error, and the process says so and stops: it
is not silently skipped, and it does not leave a Sentinel running with a master
that has no address.

### A mapped address is the address it embeds

`::ffff:127.0.0.1`, `::ffff:7f00:1` and `127.0.0.1` are one address, and every
place identity is decided says so: the replica already following that master is
following it under either spelling, and the node already in handshake at that
address is not met a second time. Sentinel keys a master by the name it was
given, and that does not change here; what changes is that the address under
the name is classified by value like every other address, so `SENTINEL MONITOR`
refuses `::ffff:224.0.0.1` exactly as it refuses `224.0.0.1`. A mapped address
is classified as the address it embeds wherever it is classified.

Printing does not change. A mapped address is still written in the RFC 5952 §5
mixed form — `::ffff:127.0.0.1`, the embedded IPv4 address in dotted-quad
and the rest in hex, which `/app/DOCS/03-address-text.md` sets out —
everywhere this tree writes one. What an address is *called* and *which address
it is* are two different questions with two different answers, and a tree that
answers them in one place loses one of them.

### What does not change

* **The tree still builds clean.** `make` from a clean tree still succeeds with
  the container's compiler and C library, reports no error and no call to a
  function whose declaration the compiler never saw, and leaves all six programs
  in `src/`; a flag given in `CFLAGS` on the `make` command line still reaches
  every compilation in `src/` and `deps/`; and nothing is silenced instead of
  fixed — no warning-disabling flag or pragma, no code commented out or
  preprocessed away, no flag that restores an older compiler's default.
* **Zone identifiers keep working exactly as they do now.** `fe80::1%lo`
  survives `CLUSTER MEET` → `CLUSTER NODES` with its text intact, and it
  survives `SENTINEL MONITOR` → the configuration file Sentinel rewrites for
  itself → a restart that reads that file back, still spelled the way it was
  given. `fe80::1%lo` and `fe80::1%eth0` are two different addresses; a zone
  written as an interface name and the same zone written as that interface's
  index are one address. A zone identifier is part of the address, not
  decoration on it.
* The order in which a multi-address name is attempted is the resolver's order
  with the RFC 8305 family interleave on top
  (`/app/DOCS/02-connection-establishment.md`). Classification tells addresses
  apart; it does not re-sort them, and it does not drop an address that lookup
  returned.
* **An address an operator binds is not a peer.** `bind` still takes the
  unspecified address and still means the wildcard, in every family this machine
  has; a listener is not a machine this tree reaches out to. Nothing here
  narrows what may be listened on, and a server given no `bind` directive still
  comes up on the wildcard.
* Everything already in this tree stays: the family-independent socket layer
  and lookup (`/app/DOCS/01-address-family-independence.md`), the RFC 8305 race
  and its timing (`/app/DOCS/02-connection-establishment.md`), canonical
  address text, bracketed `[address]:port` joins and comparison by value
  (`/app/DOCS/03-address-text.md`), and running on a host with no IPv4 —
  including that IPv4 on explicit request still works on a machine that has it
  (`/app/DOCS/04-operating-without-ipv4.md`).
* **No IPv4-only construct comes back to do the classifying.** The standing
  requirement this tree already meets still holds everywhere in it: no
  `struct sockaddr_in`, no `struct in_addr`, no `AF_INET` branch and no
  `INADDR_ANY`, `INADDR_LOOPBACK` or `inet_addr`
  (`/app/DOCS/01-address-family-independence.md`). Telling a multicast address
  from a unicast one, or a mapped address from the address it embeds, is done
  on the bytes of an `in6_addr` and on what `getaddrinfo` returned — not by
  reaching for the IPv4-only type to look at the IPv4 half with. An IPv4
  address reaches this tree as a mapped address or through the
  family-independent path, exactly as it has since that step.
* The repository's own test suite is not weakened, renamed, filtered or
  disabled.
* No new dependency, no new package and no new file in the tree are needed for
  any of this.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/05-address-classes.md` — the address
architecture this works from: RFC 4291 §2.4 and §2.5 on the classes and their
prefixes, §2.5.5.1 and §2.5.5.2 on the compatible and mapped forms and why one
is deprecated, RFC 4007 §6 and §11 on zones and the `%` notation, RFC 3879 on
`fec0::/10`, RFC 4193 on `fc00::/7`, RFC 5952 §5 on printing a mapped address,
and the IANA IPv4 Special-Purpose Address Registry. It records what the
standards say, what the C library on this machine already answers, and what this
container can and cannot show — not what this repository must do.

`/app/DOCS/01-address-family-independence.md`,
`/app/DOCS/02-connection-establishment.md`,
`/app/DOCS/03-address-text.md` and
`/app/DOCS/04-operating-without-ipv4.md` are the references for what this tree
already does, and everything they cover still holds.

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
