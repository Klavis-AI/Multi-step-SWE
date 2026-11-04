# Step 3 — Write every address one way, and know when two spellings are one address

## 1. Implement this milestone

The tree now looks a host up without naming a family and reaches it by racing
the addresses it got back. What it does not yet do is **write those addresses
down consistently**. An IPv6 address has many valid text representations — `::1`,
`0:0:0:0:0:0:0:1` and `0::1` are one address written three ways — and this
repository currently writes whichever one it happened to be handed, joins an
address to a port with a colon that is indistinguishable from the address's own
colons, and decides whether two addresses are the same by comparing their text.
An operator cannot grep a log for a node, a node cannot read back the cluster
configuration it wrote, and a client cannot follow a redirect.

**Every address this repository writes is written in one canonical form; every
place an address is written together with a port uses the bracketed form; every
place that reads such a string accepts it; and two texts that denote the same
address are treated as the same address.**

### Canonical text

Every IPv6 address this repository writes is in the form RFC 5952 §4 describes:
leading zeros suppressed, `::` used once and for the longest run of zero fields
(the first, when two runs are equally long), never for a single zero field, and
lower-case hexadecimal.

* This holds wherever an address reaches a reader: log lines at every level,
  `INFO` fields, command replies, error replies, the process title, the
  **files the tree writes for itself** — the cluster configuration file and the
  configuration Sentinel rewrites — and the messages the shipped client and
  load generator print.
* It holds **whatever spelling the address arrived in**. An address this tree
  took from a peer's socket, learned over gossip, or read back out of a file it
  wrote earlier is written canonically, not echoed.
* A field that reports back **the target an operator named** is not an address
  this tree wrote, and keeps its text. `CONFIG GET` returns what was configured;
  the master a replica reports — `INFO replication`'s `master_host`, and the host
  in the replica's own `ROLE` reply — is the argument `SLAVEOF` was given,
  unchanged. That argument may be a registered name rather than a literal, so
  re-spelling it would be answering a question about something else. Where such
  text is *joined* to a port — the process title, a log line — the join is
  bracketed and the text is still not re-spelled. The host handed to the shipped
  client or load generator with `-h` is such a target too: a message saying the
  tool could not connect to it names it as it was given, joined to its port by
  the rule below.
* An IPv4 address is written as its four decimal octets, exactly as it is today.
* Canonical form is a rule for **producing** text. Every form RFC 4291 §2.2
  allows is still **accepted** everywhere it is accepted today.

### An address and a port, written together

Where an address and a port appear as one string, an IPv6 address is enclosed in
square brackets and the port follows the closing bracket — the `IP-literal` form
of RFC 3986 §3.2.2:

```
[::1]:6379        [2001:db8::1]:16379
```

An IPv4 address and a registered name are **not** bracketed: `192.0.2.1:6379`.

Which of the two applies is decided by the text being joined and not by what
that text turned out to denote: a host whose text contains a `:` is bracketed,
one whose text does not is left alone. That is the rule under which the result
can always be taken apart into a host and a port again, and it is the same rule
whether the text is an address this tree produced or a target an operator named
and this tree is repeating back.

That join is written in one place and called, not spelled out again at each
site that builds such a string — the same rule the parse and the comparison
below are held to, and for the same reason: a rule copied per site is only as
good as its worst copy.

This covers every such string the tree writes, including the redirection errors
a cluster node replies with, the rows of `CLUSTER NODES` and of the cluster
configuration file, the address field of `INFO sentinel`, the process title, the
log lines that name a peer, and the connection-failure messages the shipped
tools print.

It does **not** cover a field that already carries the address on its own beside
a separate port field. Those are not authorities and must be left as they are:
the `ip=` and `port=` fields of `INFO replication`'s per-replica lines, the
address and port elements of `CLUSTER SLOTS` and `ROLE`, the `ip` and `port`
fields of Sentinel's instance replies, and the host and port columns of the
directives Sentinel writes into its own configuration file. Their **text**
becomes canonical under the rule above — except where that text is the operator's
own, named there — and their **shape** does not change. Bracketing a host that
sits in a field of its own does not make it more readable; it makes it a host
this tree can no longer resolve when it reads the field back.

### Reading one back

Every place this repository parses an address-and-port string accepts the
bracketed form, and the parser finds the delimiter the way RFC 3986 §3.2.2's
grammar implies: a leading `[` means the address ends at the matching `]` and the
port follows a `:` after it; otherwise the address ends at the **last** `:`.
Like the join above, that rule is written in one place the readers call rather
than restated at each of them. The helper script named below is a separate
program and states it once in its own language.

Two of these are round trips, and they are the ones that say whether the reading
half moved with the writing half:

* a node restarted after writing its cluster configuration file reads that file
  back and knows the same cluster it knew before, and a Sentinel restarted after
  rewriting its own configuration still knows the master it was monitoring;
* the shipped client, in cluster mode, follows a redirection it is given to the
  node that issued it and reissues the command there.

The helper script the repository ships for building and inspecting a cluster
writes and parses the same strings, and is in scope. The example programs under
`utils/` are not: they are demonstrations kept beside the source rather than
part of what the repository ships, and they are left exactly as they are.

### Two spellings, one address

Wherever this repository decides whether two addresses are the same address, it
decides by the **value** and not by the text. `0:0:0:0:0:0:0:1` and `::1` are one
address at every such decision — among them the client filter that selects a
connection to close by its address, the check that a replication target is the
master already attached to, the table Sentinel keeps of the instances it
monitors, and the cluster's own node table. Every comparison named here is
between two addresses of the same family, so nothing below turns on what an IPv4
address and an IPv6 address have to do with each other.

That decision, too, is made **once**, somewhere anything in this tree can
reach, rather than written out again beside each caller. A comparison repeated
per site is the same defect in more copies of itself, and the next site added
gets the old answer by default.

### Text at the boundary

An address is carried as the value it is and spelled when it is printed. This is
what makes the rule above true in general rather than at the four places it was
noticed: a program that keeps an address as text and compares the text has to be
corrected once per comparison, and a program that keeps the address has nothing
to correct.

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
* **Which** addresses are used, and in what order, is settled: the
  family-independent lookup and the RFC 8305 race already in this tree are
  untouched, including the Connection Attempt Delay and every timing property
  that came with them.
* IPv4 is still reachable, still written unbracketed, and still configured the
  way it is today. No default changes.
* Configuration and named targets are echoed, not normalised: `CONFIG GET`
  returns what was configured, a replica reports the master it was told to
  follow as it was told it, and the shipped configuration files are not
  rewritten.
* The listening side, the commands and their replies, the directives and their
  defaults, and the repository's own test suite are as they are.
* An address carrying a zone identifier (`fe80::1%eth0`) needs no interpreting:
  nothing here requires one to be resolved, matched, or given a meaning it does
  not have today. Its text still has a colon in it, so where it is joined to a
  port that join is bracketed like any other — that is the join rule above
  applying to text, not this step acquiring an opinion about scopes.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/03-address-text.md` — RFC 4291 §2.2 on the
forms an address may be written in, RFC 5952 §4 rule by rule, RFC 3986 §3.2.2's
`IP-literal` and what its grammar implies for a parser, and the POSIX conversions
between an address and its text, each with the section it comes from. It states
what the standards say and not what this repository must do.

`/app/DOCS/01-address-family-independence.md` and
`/app/DOCS/02-connection-establishment.md` are the references for the lookup
and the connection race this tree already has. Three properties they cover are relied on throughout the work
above and must still hold when it is finished: a host is looked up **once,
without naming a family**; the addresses that lookup returns are attempted by the
RFC 8305 algorithm, interleaved and overlapped; and the listening side answers on
every address it is configured for, over either family. This step changes how
addresses are **written and compared**. It does not change which ones are used,
when they are attempted, or what the listening side does.

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
