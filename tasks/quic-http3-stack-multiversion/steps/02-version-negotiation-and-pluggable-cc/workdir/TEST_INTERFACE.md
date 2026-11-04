# Test interface — step 02, version negotiation, pluggable congestion control, and the Retry integrity tag

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

This milestone turns a single-version stack into a multi-version one. Four QUIC
versions are now accepted — drafts 23, 24, 25 and 27 — with draft-27 as the
default, and a client offering anything else draws a Version Negotiation packet
instead of a handshake. That packet advertises only three of the four: draft-25
is accepted if a client offers it, but is deliberately not one of the versions
`negotiate_version` lists. Congestion control stops being hard-wired: an
`Algorithm` enum and a `set_cc_algorithm_name` setter select it by name, with
Reno as the only registered algorithm and the default. Retry packets gain the
AEAD integrity tag draft-25 introduced, replacing the inline ODCID field that
draft-23 carried. `Stats` grows a `delivery_rate` field, and the HTTP/3 layer
offers all four `h3-NN` ALPN strings rather than only `h3-23`.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

Four things that were true at step 01 stop being true here, and all four are
the kind a candidate leaves half-done.

The Retry packet changes shape. At step 01 (draft-23) it carried the original
destination connection ID inline as an explicit field and had no integrity
tag. `quiche::retry()` takes no version argument at this milestone: a
token-less Initial in any of the four supported versions draws the same
Retry, ending in a 16-byte AEAD tag instead, with no inline ODCID field.
There is no Initial version here that still draws step 01's inline-ODCID
shape -- that shape stopped being reachable the moment this step's Retry
path shipped. A stack that still writes the inline field produces a Retry whose
last 16 bytes are not a tag: the checker reads them as one, the tag does not
verify, and `cap-retry-integrity-tag` fails. Writing *both* shapes is the case
no group here can tell apart -- the bytes between the source connection ID and
the trailing tag are opaque to the checker, so a hybrid parses as a longer token
and every assertion still holds.

The HTTP/3 headers event changes shape. At step 01,
`quiche::h3::Event::Headers` was a **tuple** variant -- `Headers(Vec<Header>)`
-- and step 01's contract said so in as many words. At this milestone it is a
**struct** variant with two named fields, `Headers { list: Vec<Header>,
has_body: bool }`, where `list` is the field-line list the old variant carried
and `has_body` is true when data follows the headers on the same stream. The
checker matches it as `quiche::h3::Event::Headers { list, .. }`, so a tree that
keeps the tuple variant does not compile against the harness at all and every
requirement group is lost with it -- this is the one flip here whose cost is the
whole step rather than one group. Everything else in `h3` keeps the shape step
01 gave it.

The HTTP/3 frame type `0x0E` (DUPLICATE_PUSH) is deleted. At step 01 it parsed
into a dedicated `Frame::DuplicatePush` variant; at this milestone it is not a
known type at all, so it must parse as an unknown frame and be **ignored**
rather than rejected — HTTP/3 requires unknown frame types to be skipped, not
treated as errors. Leaving the old variant in the enum keeps the parser
accepting a frame the version no longer defines.

The example binaries' command line changes too, and it is not optional
plumbing: step 01's `client`/`server`/`http3-client`/`http3-server` took
`docopt` flags (`--listen`, `--cert`, `--key`, `--root`, `--no-verify`, and
so on). From this step through step 05 those flags do not exist -- the crate
that parses them, `tools/apps`, is not a workspace member again until step 06.
`server` and `http3-server` now take **no arguments**: they bind
`127.0.0.1:4433` and resolve `examples/cert.crt`, `examples/cert.key` and
`examples/root` relative to whatever directory they are started in, not the
crate root. `client` and `http3-client` take **exactly one** argument, a URL,
and nothing else. There is no `--no-verify` flag because there is nothing to
disable: certificate verification is unconditionally off. A binary that still
expects step 01's flags does not start under this milestone's harness, and
every group that spawns one fails for a reason that has nothing to do with
QUIC or HTTP/3.

## Interface elements the checker depends on

### `quiche::connect / quiche::accept return type, and set_max_idle_timeout`

```rust
quiche::connect(Option<&str>, &[u8], &mut Config) -> Result<Pin<Box<Connection>>>
quiche::accept(&[u8], Option<&[u8]>, &mut Config) -> Result<Pin<Box<Connection>>>

Config::set_max_idle_timeout(u64)      // was set_idle_timeout
```

Step 01 returned a bare `Box<Connection>` from both `connect` and `accept`. At
this milestone the `Connection` is self-referential and must not move after
creation, so both now return `Pin<Box<Connection>>` instead. Connection IDs
are still bare `&[u8]`; the `ConnectionId` newtype does not arrive until step
04.

`Config::set_idle_timeout` is renamed `Config::set_max_idle_timeout` at this
milestone. The argument (`u64`, milliseconds) and behaviour are unchanged; the
old name does not exist here or at any later step.

### `quiche::CongestionControlAlgorithm`

A public enum naming the available congestion control algorithms. At this
milestone it has exactly one variant, `Reno`, with discriminant 0. It is
re-exported at the crate root, so `quiche::CongestionControlAlgorithm::Reno`
resolves.

### `quiche::Config::set_cc_algorithm_name(name: &str) -> Result<()>`

Selects the congestion control algorithm by its string name. `"reno"` is
recognised; any other string returns `Err`. The default when this is never
called is Reno.

### `quiche::Config::set_cc_algorithm(algo: CongestionControlAlgorithm)`

The enum-typed twin of `set_cc_algorithm_name`. Takes the algorithm directly
rather than by name, so it cannot fail and returns nothing.

### `quiche::Stats::delivery_rate`

A public `f64` field on the `Stats` struct returned by `Connection::stats()`,
reporting the estimated data delivery rate in bytes per second. Present at this
milestone; absent at step 01. It is an instantaneous rate *sample* —
`delivered_in_interval / max(send_elapsed, ack_elapsed)` — not an average over
the whole connection, and it is already non-zero once the handshake has
completed; there is no zero-before-data-flows baseline to observe. The way to
bind it is to change the delivery rate over a sustained transfer and watch the
estimate move with it -- which is what the group does. Nothing reads the field
between the handshake and the first data, so the "already non-zero" clause is
a statement about the estimator rather than something a group checks; getting
it wrong costs nothing at this step.

### `quiche::PROTOCOL_VERSION`

The version a `Config` is built with by default. At this milestone it is
`0xff00001b` (draft-27), not the `0xff000017` (draft-23) of step 01. Three
other versions are accepted by `version_is_supported`: `0xff000019`
(draft-25), `0xff000018` (draft-24) and `0xff000017` (draft-23). Accepted is
not the same as advertised: `negotiate_version`'s Version Negotiation packet
lists only draft-27, draft-24 and draft-23. draft-25 is accepted if a client
offers it, but is deliberately not one of the three the packet lists — see
`cap-version-negotiation`.

### `the draft-24, draft-25 and draft-27 Initial salt`

Step 01 gave the Initial salt for draft-23:
`c3eef712c72ebb5a11a7d2432bb46365bef9f502`, used in HKDF-Extract to derive the
Initial secrets from a connection's original destination connection ID (RFC
9001 Section 5.2, whose own text is v1-specific and predates every draft this
release supports).

That value is unchanged for every version at this milestone. Drafts 24, 25
and 27 do not introduce a new salt; the salt only ever changes at draft-29 (a
later step). A candidate that derives a new value for draft-27 because RFC
9001 Section 5.2 implies later versions get their own is solving a problem
this release does not have.

### `quiche::h3::APPLICATION_PROTOCOL`

The ALPN wire encoding the HTTP/3 layer offers, as a length-prefixed sequence:
`\x05h3-27\x05h3-25\x05h3-24\x05h3-23`. Step 01 offered only `\x05h3-23`.
This list's order is not itself observable: `select_alpn` walks the *client's*
list outermost and returns the first name the server also knows, so the
client's ordering decides the outcome, not the server's. What is observable,
and what is checked, is membership — h3-27 is now accepted, h3-23 still is, and
a name outside these four (h3-29, the next one this library grows into) is
not.

### `quiche::h3::Event::Headers`

A **struct** variant, not the tuple variant step 01 had:

```rust
#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    Headers { list: Vec<Header>, has_body: bool },
    Data,
    Finished,
}
```

The checker matches it as `quiche::h3::Event::Headers { list, .. }` and also as
`quiche::h3::Event::Headers { list, more_frames }` at later steps, so the first
field is named `list`; the second is `has_body` here. A tree that keeps step
01's `Headers(Vec<Header>)` does not compile against the harness, which is why
"the flip" above calls this the expensive one. `h3::Config::new()`,
`h3::Connection::with_transport`, `send_request`, `send_response`,
`send_body`, `recv_body`, `h3::Header::new/name/value`, `h3::Error::Done` and
`h3::APPLICATION_PROTOCOL` all keep the shapes step 01 gave them.

### `quiche::negotiate_version(scid, dcid, out) -> Result<usize>`

Writes a Version Negotiation packet into `out`, echoing the connection IDs from
the client's Initial with source and destination swapped, and listing the
versions this build supports. The caller reaches it after `Header::from_slice`
reports a version that `version_is_supported` rejects.

### `the Retry integrity constants for drafts 25 and 27`

RFC 9001 Section 5.8 describes the computation, and the computation is exactly
the one this milestone needs: AEAD_AES_128_GCM over the Retry pseudo-packet --
the original destination connection ID, length-prefixed, followed by the Retry
packet with its trailing 16-byte tag removed. The **constants Section 5.8 prints
are a different matter**. They are QUIC v1's, and v1 does not exist at this
milestone. Using them produces a tag that verifies against nothing the checker
computes.

The key and nonce this milestone actually uses -- shared by drafts 25 and
27, the two versions the standards process gave this tag to -- are:

* key   `4d32ecdb2a2133c841e4043df27d4430`
* nonce `4d1611d05513a552c587d575`

`quiche::retry()` takes no version argument at this milestone and always
builds the tag-based packet above, whichever of the four supported versions
the triggering Initial named: a token-less Initial in any of them draws the
same Retry, wire-versioned 0xff00001b (draft-27) and ending in this 16-byte
tag. There is no Initial version at this milestone that still draws step
01's inline-ODCID Retry -- that shape stopped being reachable the moment
this step's Retry path shipped. `cap-retry-integrity-tag` drives this with a
draft-27 Initial; nothing about the answer would differ if it used a
draft-23, -24 or -25 one instead, since the server does not look at which
version asked.

`spec/appendix-a-vectors.json` records `retry_integrity_key` and
`retry_integrity_nonce` as Section 5.8's values, and `initial_salt` as Section
5.2's value, because those are what the document it was extracted from
prints. **All three entries are QUIC v1's, v1 does not exist at this
milestone, and none of the three apply to any version here; where they and
this section disagree, this section governs.** Every other value in that file
is still good.

### `client and server example binaries`

Same four names as step 01 -- `client` and `server` for raw QUIC,
`http3-client` and `http3-server` for HTTP/3 -- built by
`cargo build --examples` at the workspace root. **The command line is not
the same as step 01's; see "The flip" above.** This grammar holds from this
step through step 05, since the flag-taking binaries under `tools/apps` are
not a workspace member again until step 06:

* `server` and `http3-server` take **no arguments**. They bind
  `127.0.0.1:4433`, and load `examples/cert.crt` and `examples/cert.key`
  relative to the working directory they are started in, and serve files out
  of `examples/root` relative to the same directory -- not the crate root and
  not the executable's own location.
* `client` and `http3-client` take **exactly one** argument, a URL, and
  nothing else. Certificate verification is unconditionally off; there is no
  `--no-verify` flag or any other way to turn it back on.

**Do not name any of the four `quiche-client` or `quiche-server` at this
step.** Those names belong to a later milestone's single do-each-job program,
and the checker does not want them here: it resolves a binary by trying
`quiche-client` before `client` (and `quiche-server` before `server`), and it
decides the command-line grammar from that name -- anything called `quiche-*`
is handed the later generation's `docopt` flags (`--no-verify`,
`--wire-version`, `--listen`, `--cert`, …), which a program written to this
step's argument-free grammar reads as a URL and fails on. A candidate that
also ships a `quiche-client` therefore has the gate groups run the wrong
program with the wrong flags, whatever the four documented names point at.
The rule from step 01 therefore still holds: four names, and no `quiche-`
prefix among them.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-version-negotiation` | requirement | T2 | The checker sends a well-formed Initial carrying the unrecognised version 0x6b3343cf to the candidate's server. The server answers with a Version Negotiation packet: long-header form, a version field of zero, the client's source connection ID as its destination and the client's destination connection ID as its source, and a supported-version list containing exactly three entries, in this order: 0xff00001b (draft-27), 0xff000018 (draft-24) and 0xff000017 (draft-23). draft-25 (0xff000019) is accepted by `version_is_supported` if a client offers it, but is deliberately not one of the three this list advertises — adding it is the most plausible wrong answer. The server must not answer with a server Initial. |
| `cap-draft27-default` | requirement | T2 | A Config built with the crate's own default version produces a client Initial whose wire version is 0xff00001b, and that Initial's payload decrypts with the draft-27 Initial salt. A server offered a draft-27 Initial completes the handshake rather than negotiating, while the same server still completes a handshake against a draft-23 Initial — all four versions are live, draft-27 is merely preferred. |
| `cap-retry-integrity-tag` | requirement | T2 | The checker sends a token-less Initial and the candidate's server answers with a Retry. The packet ends in a 16-byte AEAD_AES_128_GCM tag that verifies against the Retry pseudo-packet built from the checker's original destination connection ID, and it carries no inline ODCID field — the byte count between the source connection ID and the token accounts for the token alone. Replaying an Initial that carries the issued token then proceeds to a handshake instead of drawing a second Retry. |
| `cap-pluggable-cc` | requirement | T3 | `Config::set_cc_algorithm_name("reno")` returns Ok and `set_cc_algorithm_name` on an unregistered name — "cubic", "bbr", "" — returns Err rather than silently falling back. `set_cc_algorithm` accepts `CongestionControlAlgorithm::Reno` directly. A connection built through either path, and one built with neither, all transfer 64 KiB intact, and their `cwnd` grows from the initial window under acknowledged data: the selector is wired to a real controller, not stored and ignored. |
| `cap-delivery-rate-stats` | requirement | T3 | `Connection::stats()` returns a `Stats` carrying a `delivery_rate` field. After a sustained 256 KiB transfer over a link the harness paces, it is positive and within an order of magnitude of the bytes-acknowledged-over- elapsed-time the harness measures independently. The other five fields step 01 established are still present and still move. |
| `cap-duplicate-push-removed` | requirement | T5 | An HTTP/3 frame whose type field is the varint 0x0E, carrying a one-byte payload, is parsed as an unknown frame and ignored: the parse succeeds, no dedicated DUPLICATE_PUSH variant is produced, no connection error is raised, and a following HEADERS frame on the same stream is still delivered. Step 01 parsed this type into a distinct variant; here the type is not defined and HTTP/3's rule for unknown types — skip it — applies. |
| `cap-h3-27-alpn` | requirement | T1/T2 | The candidate's `http3-client` and `http3-server` complete a GET over the `h3-27` ALPN, and the ALPN the server advertises is the four-entry length-prefixed list `\x05h3-27\x05h3-25\x05h3-24\x05h3-23`. A client restricted to `h3-23` alone still completes: the older protocols remain on offer rather than being replaced. A client offering only `h3-29` — the next name this library will grow into, not yet one of the four — selects nothing: the handshake does not settle on a shared application protocol. |
| `reg-01-initial-keys` | gate | T2 | The checker opens the candidate's client Initial with its own draft-23 keys -- salt `c3eef712…` over the client's destination connection ID -- and finds a TLS ClientHello at the head of the CRYPTO stream; it then opens the server's reply, keyed from that same original destination connection ID with the server labels, and finds a ServerHello. The draft-23 handshake then completes. Draft-23 is still a supported version at this milestone, so its key schedule must still be right. |
| `reg-01-h3-get` | gate | T1 | The candidate's `http3-client` performs a GET against the candidate's `http3-server` and receives the exact body. The ALPN list grew here, which is exactly what plausibly breaks it. |
| `reg-01-stream-transfer` | gate | T1 | The candidate's `client` fetches a 512 KiB pseudorandom file from the candidate's `server` over UDP loopback byte for byte. Congestion control became pluggable here, which is exactly what plausibly breaks it. |
| `gate-endtoend` | gate | T1 | The stack works at all: the candidate's workspace builds with `cargo build --offline` at the root, the `server` starts, the `client` completes a handshake against it over UDP loopback and retrieves a small file, and the client exits zero. If this fails, every other T1 observation at this step is noise. |

Surfaces: **T1** your client and server as processes over UDP
loopback; **T2** a QUIC endpoint the checker implements itself, reading
and writing real packets; **T3** your library linked into the checker's
own crate, public API only; **T4** a file or layout your code produces;
**T5** an input with exactly one correct answer.

## How this step is checked

The **gate** covers proof of life plus the milestones you already have: your
workspace builds, your binaries complete one end-to-end exchange, and the
earlier capabilities this step disturbs still work. Gate groups add no new
requirement of their own, but a failure in one means the step is not
satisfied, however much of the new work is right.

The **requirement** groups cover this step's own new capability, and every one
of them is in scope. Your step's interface document lists every group with its
tier.

Every case within a group must pass, and there is no separate requirement for
style or for extra work.

Three families of group, named in the report so you can tell them apart:

| prefix | tier | what it checks |
|---|---|---|
| `cap-*` | requirement | the capability this step introduces |
| `reg-NN-*` | gate | a capability step NN introduced, still working |
| `gate-endtoend` | gate | one end-to-end exchange through your own binaries |
| `candidate-workspace-builds` | gate | your workspace builds at all, before any other group runs |

`reg-NN-*` groups are not decoration: every step in this task rewrites
something an earlier step built, and the `reg-` groups are chosen from the
surfaces this step disturbs. (A step with no earlier milestone carries none of
them: this section describes the family, and the group table above is what
says whether a given step has any.) A rewrite that quietly drops an older
behaviour does not fail just one group; it means the step is not satisfied.

The corollary is worth stating plainly: a do-nothing continuation satisfies
none of the new work. A workspace carried forward untouched passes every gate
group — it has not broken anything — and passes no requirement group, because
it implements none of this step's capability.

### What the checker is allowed to look at

Behaviour, and only behaviour:

* your library, linked into a test harness that lives outside `/app` and calls
  your public API;
* your client and server binaries -- whichever names this step's own interface
  section gives them -- run as real processes talking to each other over UDP
  loopback;
* your binaries talking to a QUIC endpoint the checker implements itself, which
  derives its own keys and reads the bytes on the wire;
* files your code emits — qlog, netlog, a generated C header, the workspace
  layout;
* exact answers to inputs designed to have exactly one right answer.

It never **checks** anything by reading your source: no group greps your tree
for an identifier, counts your modules, or checks a log line. One qualification
belongs beside that: anything the checker's own crate *imports or names* has to
exist under that name, because it is a Rust program that compiles against your
crate. Those names are this step's interface section, and where a group builds
a probe of its own, the names that probe imports are written down there too.
Inside your own modules, name things whatever you like. Two plumbing
steps outside the checked path do read source text -- the image picks which
pinned BoringSSL to link by looking at `src/tls.rs`, and where this
milestone's release changed an API's shape the harness picks which dialect of
its own test shim to compile by looking for the new spelling in `src/`. Both
read live code only -- `//` comments are stripped before either check, and the
dialect probe, where this step has one, drops `/* ... */` blocks as well -- and
both key on spellings this step's own documents already tell you to change, so a
tree you have converted is never misread.
Name things whatever you like, put them wherever the manifest says, and write
in whatever style you prefer.

### Reading the report

`reward_details.json` names every group, its tier, whether it
passed, and for a failing group the first line of why. When the gate fails it
says which gate group failed and what it saw — a step reported as unsatisfied
without naming the group responsible would be indistinguishable from a
candidate that did nothing. When a group fails for a reason that is not about
your code — a build that did not finish inside its budget, a harness that
failed to link — it is reported as such rather than silently treated as a
failure.

## The build environment

Written once and generated into every step's copy of this document, so that the
fifteen copies cannot drift apart. Anything in this section that is *not* true
of your step is called out in the step's own section above, never by silently
contradicting it here.

### Toolchain

Rust **1.88** (`rustc 1.88`, `cargo 1.88`), Debian bookworm, x86-64. Also
installed: `clang`, `cmake`, `ninja`, `perl`, `go`, `pkg-config`, `git`,
`python3`, `jq`, `ripgrep`, `xxd`, `iproute2`, `libev`, `uuid-dev`.

### BoringSSL is prebuilt; your build script must link it, not build it

Your workspace does **not** contain BoringSSL and must not try to fetch or
compile it. A prebuilt static BoringSSL is installed in the image, and the
environment variable **`QUICHE_BSSL_PATH`** points at it:

```
$QUICHE_BSSL_PATH/
├── build/
│   ├── libcrypto.a
│   ├── libssl.a
│   ├── crypto/libcrypto.a     (hard link, for the older expected layout)
│   └── ssl/libssl.a           (hard link)
├── libcrypto.a                (copy, for the newest expected layout)
├── libssl.a
├── include/openssl/...
└── COMMIT                     the BoringSSL commit this was built from
```

All three layouts are populated, so a build script that looks in any of them
finds the archives. The archives are built with
`-DCMAKE_POSITION_INDEPENDENT_CODE=ON`, so they link into a `cdylib`.

`QUICHE_BSSL_PATH` is set in the environment in the checker image, and in the
implementation container your step's `.cargo/config.toml` is what sets it —
keep that file, or a build script that reads the variable has nothing to read
and nothing to link. Read it in your build script and emit the
`cargo:rustc-link-search` / `cargo:rustc-link-lib` lines yourself — nothing
does that for you.

The prebuilt BoringSSL that `QUICHE_BSSL_PATH` points at is the one your step
is built against. The measurement runs offline, so it is also the only one available:
you never need to know its commit hash, only to link against that path.

### The measurement runs offline

At measurement time the container has **no network**. `CARGO_NET_OFFLINE=true` is
set and the registry cache holds the crates this milestone's build can reach:
every crate in the `Cargo.lock` that ships in this workdir, the versions this
milestone's own manifests resolve to, the previous release's graph, and the
checker's own harness crate's graph. Nothing else is available. Consequences:

* **Keep `Cargo.lock`.** Do not delete it and do not run `cargo update`. It is
  the contract between your manifest and the offline cache, and a version it
  pins is reachable offline even where the requirement alone would not reach
  it.
* **A dependency you add that is outside that set will not resolve**, here or
  at measurement time, and the build failure is reported as a build failure. If you
  need something, check `Cargo.lock` first.
* **Your resolution is pinned to this milestone's set, here as well as at
  measurement time.** One image serves all fifteen milestones and its registry
  cache is the union of every milestone's crates, so the set above is not what
  the cache holds -- it is what you are allowed to resolve. The step's
  `setup.sh` points cargo's `crates-io` at a single directory source built
  from this milestone's own locks, before your first command runs. A
  dependency outside it therefore fails here too, with the checker's own
  message:

      no matching package named `NAME` found
      location searched: directory source `/opt/vendor/NN`

  A local build that resolves is a build that works here as well. Do not delete or edit
  `$CARGO_HOME/config.toml`; that file is the pin.
* Cargo resolves the *whole* workspace graph, dev-dependencies included, before
  it compiles anything — so even a dev-dependency you invent breaks the build.
* `CARGO_TARGET_DIR` is set to a shared directory whose dependency graph is
  already compiled. Your build pays for your code only.

### What the checker builds and runs

```
cd /app && cargo build --offline          # libraries and binaries
cd /app && cargo build --offline --examples   # best effort; never fatal
cd /app/<app package> && cargo build --offline   # best effort; never fatal
```

Your own tests are **not** compiled by the checker, and a broken test of yours
costs you nothing. Correspondingly, passing your own tests is not required:
the checker links your crate from its own throw-away harness crate outside
`/app`, and runs your binaries as processes.

### Where your crate has to be

The checker locates the transport crate by looking for a `Cargo.toml` whose
package `name` is `quiche`, first at `/app/quiche` and then at `/app`, and uses
the first of the two that exists. Every group that merely needs *the transport
crate* -- to build it, to read its manifest, to look inside its `src/` or its
`include/` -- goes through that search, so either arrangement satisfies all of
them.

The exceptions are the groups for which the layout is the thing being checked,
and there are two kinds. The step that *introduces* the workspace conversion
checks the move itself, against the paths that conversion is defined to
produce. From that step onward, a group that checks **workspace membership**
names the member directories it expects -- `quiche` among them -- because which
crates are members, and where they sit, is that milestone's subject matter
rather than an incidental path. A group of either kind says so in its own
interface section. Outside those groups the layout is never checked.

### The binaries

Several groups run your client and server as real processes over UDP loopback.

**This step's own interface section names the exact binaries it expects, and
that section governs.** Build them with a plain `cargo build` (or let the
best-effort `--examples` pass pick them up). Where they live does not matter:
`examples/`, `tools/apps/` or their own package are all fine. The checker
searches these directories, in this order, for each name before it moves to the
next name:

```
$CARGO_TARGET_DIR/debug/
$CARGO_TARGET_DIR/debug/examples/
$CARGO_TARGET_DIR/release/
$CARGO_TARGET_DIR/release/examples/
```

and then, in case a candidate ignored `CARGO_TARGET_DIR` or a workspace member
carries a target directory of its own, the same four in the same order under
`<workspace root>/target/`, `<workspace root>/quiche/target/` and
`<workspace root>/apps/target/`.

What *does* matter is the name, and it is not a free choice. Each binary the
checker needs is resolved from a short list of accepted names -- this step's own
interface section gives them, most-preferred first -- and the search is
name-major: the first name on that list is looked for across every directory
before the second is tried anywhere. A binary that is correct but named
something outside the list is invisible to the group that needs it, and the
group fails with no diagnostic.

Two consequences worth stating plainly, because both have bitten:

* A single release can ship more than one client, and they are not
  interchangeable — an HTTP/3 group needs the HTTP/3-capable binary
  specifically. Naming a general-purpose client something an HTTP/3 group
  resolves would hand that group a client that cannot speak HTTP/3.
* Upstream renamed these binaries across the fifteen releases. Do not carry a
  name forward from an earlier step because it worked there; read this step's
  interface section.

## Where this step starts

`/app` contains everything step 01 left behind, unchanged. The milestone before this one is modelled on quiche 0.1.0; this one is modelled on 0.3.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

## Out of scope

Stated so you do not spend the budget on them.

* **Interoperability with other QUIC implementations.** No second stack is
  installed and there is no network at measurement time.
* **Performance.** Throughput, CPU, allocation counts and congestion-control
  *efficiency* are never measured. Congestion controllers are checked on
  observable state machine behaviour, not on how fast they go.
* **Anything under `fuzz/`**, and your own test suite: the checker does not
  compile either.
* **Documentation, comments, formatting, clippy.**
* **Platform support other than x86-64 Linux**, and every Windows, macOS, iOS
  or Android code path.
* **The `boringssl-boring-crate` feature** at the releases where it is
  optional. Its crate version is yanked from crates.io and cannot be resolved
  offline; the default feature set is what gets built.

