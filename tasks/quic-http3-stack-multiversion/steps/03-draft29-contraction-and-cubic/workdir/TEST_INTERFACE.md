# Test interface — step 03, draft-29, version contraction, and cubic congestion control

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

This milestone adds draft-28 and draft-29 while removing drafts 23, 24 and 25,
leaving three supported versions: 27 (inherited from the previous step), 28
(new here) and 29 (new here). Read the arithmetic carefully: the previous
milestone supported four versions -- drafts 23, 24, 25 and 27 -- so draft-28 is
work you have to do, not a version you are carrying forward. The Initial salt
and Retry integrity key are now functions of the QUIC version, with draft-29
using distinct values. CUBIC becomes the default congestion control algorithm
with HyStart++ enabled for slow start. Connection::stream_priority() appears in the connection API, matching the
urgency/incremental model RFC 9218 defines for HTTP/3 extensible priorities --
the wire-level Priority header field, its u= and i parameters, and the
PRIORITY_UPDATE frame are not part of this release and arrive at 0.14.0 (step 7).
Connection ID authentication and transport-integrated qlog also land in this
release. qlog is wired in behind the "qlog" Cargo feature -- the qlog data-model
crate has sat unused at tools/qlog since the previous step, and this milestone
adds the optional dependency edge and the Connection::set_qlog method that uses
it -- and is exercised below. PTO probe changes land as well but, like
Connection ID authentication, are not independently checked at this milestone --
see "required to exist, out of scope at this step" below for why.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

Three versions that were negotiable at the previous step—drafts 23, 24, and 25—are 
now unrecognised and must draw a Version Negotiation packet. The Initial salt changes 
from a constant to a version-dependent value, affecting key derivation for every 
observed Initial packet.

## Interface elements the checker depends on

### `quiche::Config transport-parameter setters`

The setter that was `set_max_packet_size(v: u64)` up to the previous step is
now `set_max_udp_payload_size(v: u64)`, with the same units and the same
meaning. The old name is gone.

Everything else the previous step's `Config` exposed keeps its name and its
signature: `set_max_idle_timeout(v: u64)` in milliseconds,
`set_initial_max_data(u64)`, `set_initial_max_stream_data_bidi_local(u64)`,
`set_initial_max_stream_data_bidi_remote(u64)`,
`set_initial_max_stream_data_uni(u64)`, `set_initial_max_streams_bidi(u64)`,
`set_initial_max_streams_uni(u64)`, `set_application_protos(&[u8])` in wire
format, `load_cert_chain_from_pem_file`, `load_priv_key_from_pem_file`,
`verify_peer(bool)`, `grease(bool)`, `set_disable_active_migration(bool)` and
`enable_early_data()`.

The checker builds every in-process connection it measures through this
`Config`, and it compiles against whichever of the two spellings your tree
has. The rename is therefore not itself observed: keeping `set_max_packet_size`
costs nothing at this step. Do it anyway -- this contract will not name the old
spelling again, and every later step assumes the new one.

### `quiche::Config::set_cc_algorithm_name(name: &str)`

Selects the congestion control algorithm by name. At this milestone "reno" and
"cubic" are recognised; any other string returns Error::CongestionControl. The
default is CUBIC.

The name resolves to `quiche::CongestionControlAlgorithm`, a `#[repr]` enum
whose discriminants are part of the crate's public surface (they cross the C
FFI boundary unchanged and are carried verbatim into qlog output). At this
milestone it has exactly two variants: `Reno = 0`, `CUBIC = 1`. A later
milestone that adds a third algorithm is expected to append it after CUBIC
rather than renumber either of these two.

### `the recovery event path's order`

The congestion-control groups read `Stats::cwnd` at a point where its value is
decided by the order the recovery module processes a single acknowledgement,
not by the controller's arithmetic alone. That order is RFC 9002's, and
Appendix B's pseudocode is normative for it here. Two rules, and both are
observable through `cwnd`:

* For one acknowledgement, `DetectAndRemoveLostPackets` and `OnPacketsLost` run
  **before** `OnPacketsAcked` (Section B.4). A burst whose first datagram is
  dropped and whose remaining datagrams are delivered therefore produces the
  multiplicative decrease and no window growth at all, and the ratio of the
  two `cwnd` readings is the algorithm's beta and nothing else.
* `OnPacketAcked` returns without growing the window while
  `InCongestionRecovery(acked_packet.time_sent)` holds (Section B.5), so an
  acknowledgement that arrived inside a recovery period never opens the window.

`spec/rfc9002.txt` ships in the workspace. A recovery module that acknowledges
first and detects loss afterwards -- or that grows the window for an ack from
inside the recovery period -- reports a different `cwnd` at the moment these
groups sample it and fails `cap-cubic-default`.

### `quiche::Config::enable_hystart(v: bool)`

Controls whether HyStart++ is used during slow start. Enabled by default at this 
milestone. Applies to both Reno and CUBIC.

### `quiche::Connection::stream_priority(stream_id: u64, urgency: u8, incremental: bool) -> Result<()>`

Sets HTTP/3 priority parameters for a stream. Urgency ranges from 0 (highest) to 
7 (lowest); incremental is a boolean. This method appears at 0.5.0 and is called 
internally by h3::Connection::send_response_with_priority.

**It is fallible, and the return type is part of the contract.** It returns 
`Result<()>`, not `()`: the target stream is created if it does not already 
exist, and that creation can fail -- against the stream limit, or because the 
stream was already closed and collected. An infallible signature is not an 
acceptable alternative here. The case calls it and inspects the returned
`Result`, so a candidate who declares it as returning `()` does not merely lose 
one group: every `cap-*` group at this step compiles into a single test binary, 
so the binary fails to build and all of them fail.

**That coupling is stated here because it is not removed here.** The eight 
cases share one `cap` test binary, and every binary the harness builds
links the same helper surface in its own `src/pair.rs`, which names your crate's 
types directly; a compile error anywhere in either fails every group in that 
target, not only the group whose capability is missing. Splitting the cases into 
one binary per capability is not attempted at this step -- it is a change to 
this verifier's target layout and to the wiring that checks it, and the coupling
it would narrow is the one written down in this paragraph. What is real is that once the binary builds, the eight requirement
groups are checked independently: each is run and checked on its own
assertions, in its own invocation, and one failing
leaves the others' results untouched.

### `quiche::Connection::set_qlog(writer: Box<dyn std::io::Write + Send>, title: String, description: String)`

Installs a qlog sink on a live connection. Called any time after the
Connection is created, it must start producing structured output during the
handshake that follows and keep producing more as the connection is driven
further -- not a single snapshot taken at call time.

This lands the same way upstream quiche did it: behind a Cargo feature
literally named "qlog", gating an optional dependency on the qlog data-model
crate that has sat at tools/qlog, present but unused, since the previous step.

The checker's harness activates the feature on its own dependency edge, but only
after reading your manifest. All three legal ways of declaring it are
recognised -- a `qlog = []` entry in a `[features]` table, an inline
`optional = true` dependency, and a `[dependencies.qlog]` table with
`optional = true` -- and when one of them is present the harness asks for the
feature. When none is, it drops the request instead of failing to resolve your
crate. Two consequences:

* An **unconditional** `set_qlog`, with no feature at all, is reachable and
  passes; it is not the shape this milestone asks for, but it is not punished.
* A feature under a **different name** is fatal, and in the way the previous
  section describes: the method is left out of the build the harness compiles,
  so every group in `cap` fails to link rather than one.

### `the example binaries`

Four programs the checker runs as processes: `client`, `server`,
`http3-client`, `http3-server`, built by `cargo build --examples` at the crate
root and found under `target/{debug,release}` or
`target/{debug,release}/examples`. `tools/apps` is not a member of this
milestone's package and is not built; these four are the whole T1 surface.
Their command line is positional and unchanged from the previous step:

* `server` and `http3-server` take **no arguments**. They bind
  `127.0.0.1:4433`, load `examples/cert.crt` and `examples/cert.key` relative
  to the working directory they are started in, and serve files out of
  `examples/root` relative to the same directory. The checker starts them with
  their working directory set to a scratch tree laid out that way and stages
  its payload into `examples/root/`; it never writes into `/app`. Because the
  port is fixed, the checker runs the groups that need it one at a time.
* `client` and `http3-client` take **exactly one argument**, a URL, and write
  the response body — and nothing else, no status line and no framing — to
  stdout. A body that is not valid UTF-8 is written through unchanged; the
  checker compares the bytes. Peer verification is compiled off; there is no
  flag for it and none is passed.
* `server` speaks HTTP/0.9: a request is the line `GET <path>\r\n` on a
  client-initiated bidirectional stream, and a path it cannot open is answered
  with the body `Not Found!\r\n`. `http3-server` answers a missing path with
  a 404 and a non-GET method with a 405.
* `server` does address validation on every connection: a token-less Initial
  is answered with a Retry, and the checker reads that Retry off the wire.

### `required to exist, out of scope at this step`

Two more capabilities land in this release and are real, user-visible parts
of the milestone, but neither is independently checked below. Getting either
wrong costs nothing at this step, but each is called out so a later step's
verifier -- or a reader comparing this document to the release map -- does
not mistake the silence for an oversight.

* Connection ID authentication. Once the first flight of handshake data is
  processed, the transport parameters the peer advertised are checked against
  the connection IDs actually seen on the wire (initial_source_connection_id,
  original_destination_connection_id, and, on a connection that went through
  Retry, retry_source_connection_id), and a mismatch tears the connection down
  with a transport-parameter error. The difficulty is triggering the mismatch
  deterministically: a real client's source connection ID is authenticated
  as part of the AEAD tag over the very packet that carries it, so a verifier
  cannot forge a disagreeing value by editing bytes after the fact without the
  edit itself failing to decrypt -- and it cannot present two independent
  clients to the same in-process connection either, because Initial key
  derivation there latches to the first packet's destination connection ID
  and a second, differently-keyed packet simply fails to decrypt. Testing the
  mismatch honestly needs a hand-rolled, BoringSSL-parseable ClientHello whose
  encoded transport parameters disagree with its own wire header -- real TLS
  encoding machinery this milestone does not otherwise need -- so it is left
  out of scope here rather than tested by a shortcut that would not actually
  exercise the check.
* PTO probe changes. Like cap-hystart-default below, the only observable
  difference is timing, and this one has no test already accepting a
  real-clock dependency to piggyback on: reaching a PTO means driving an
  idle connection until its retransmission timer fires, which this library
  exposes no way to do without an actual wall-clock wait of the timer's own
  duration.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-draft29-salt` | requirement | T2 | A client Initial packet with wire version 0xff00001d decrypts successfully using the draft-29 Initial salt (afbfec289993d24c9e9786f19c6111e04390a899), and fails when decrypted with the draft-27 salt. |
| `cap-drafts-removed` | requirement | T2 | Client Initial packets with wire versions 0xff000017 (draft-23), 0xff000018 (draft-24), and 0xff000019 (draft-25) each provoke a Version Negotiation packet listing only 0xff00001b, 0xff00001c, and 0xff00001d, never a server Initial. The Version Negotiation packet must also **echo the client's connection IDs back swapped**, as RFC 9000 Section 6.1 requires: its Destination Connection ID is the client's Source Connection ID, and its Source Connection ID is the client's Destination Connection ID. This is inherited behaviour rather than new work at this milestone, but the case asserts it, so it is stated here rather than left for a candidate to infer. |
| `cap-cubic-default` | requirement | T3 | A Connection created with Config::new and no explicit set_cc_algorithm_name call uses CUBIC. After a congestion event, cwnd is reduced by a factor of 0.7 (CUBIC's beta) rather than 0.5 (Reno's). A fresh Config's set_cc_algorithm_name also rejects a name that is neither "reno" nor "cubic" with Error::CongestionControl. |
| `cap-h3-priorities` | requirement | T1 | The h3-29 ALPN is read out of the http3-client's own ClientHello: the checker catches the client's first flight on a socket of its own, opens the Initial with its draft-29 keys and finds `h3-29` offered ahead of any older h3 name. The http3-server and http3-client binaries then complete an HTTP/3 GET against each other and the body arrives byte for byte. This requires that **draft-29 (0xff00001d) is the wire version the client sends by default**, with no flag needed to select it. All three of draft-27, draft-28 and draft-29 are supported at this milestone, but the checker opens that first flight with draft-29 keys alone: a client that defaults to one of the other two produces an Initial the checker cannot decrypt, and the group fails even though the version it chose is a supported one. |
| `cap-retry-integrity-draft29` | requirement | T2 | A token-less draft-29 Initial sent to the candidate's server draws a Retry packet whose integrity tag verifies under the draft-29 Retry key and nonce (ccce187ed09a09d05728155a6cb96be1 / e54930f97f2136f0530a8c1c) over the Retry pseudo-packet built from the original destination connection ID, and which differs from the tag the pre-draft-29 key (4d32ecdb2a2133c841e4043df27d4430) computes over the identical pseudo-packet. The Retry carries a token and no inline ODCID. |
| `cap-hystart-default` | requirement | T3 | *(This is the one requirement group at this step whose measurement is taken over real elapsed time: the round-trip increase HyStart++ reacts to can only be produced by an RTT that actually rose, and this milestone's library has no clock-injection point. The harness sizes the margin generously — five times the detector's ceiling — and compares the default against both explicit settings, so a slow host does not decide the outcome; a stop-the-world stall long enough to distort a whole round would.*) A Connection created with Config::new has HyStart++ enabled. Holding the congestion control algorithm at Reno so that enable_hystart is the only thing varying, a connection whose round-trip time rises between slow-start rounds leaves slow start for Limited Slow Start before any packet is lost, and its congestion window after five rounds is several times smaller than that of an otherwise identical enable_hystart(false) connection given the same treatment. A Config::new connection tracks the enable_hystart(true) one. |
| `cap-stream-priority-api` | requirement | T3 | Calling Connection::stream_priority(stream_id, urgency, incremental) on a live HTTP/3 request stream, on both ends, succeeds for every urgency from 0 to 7 and both values of incremental, and the request still completes afterwards with its response headers and its whole body. |
| `cap-qlog-wired` | requirement | T3 | A writer installed with Connection::set_qlog before the handshake starts has received some structured output by the time the handshake completes, and strictly more after a further round of application data crosses the connection -- ruling out both a stub that never writes and a stub that writes one snapshot at installation and nothing again. The bytes' internal schema (qlog's own JSON structure, event names) is not inspected; only that real, growing output reaches the writer this milestone's set_qlog is given. |
| `reg-01-initial-keys` | gate | T2 | The first datagram the candidate's client sends is opened by the checker's own implementation: Initial secrets from the salt for the version on the wire, header protection removed with the AES-ECB mask, packet protection removed with AEAD_AES_128_GCM, and the plaintext then contains a CRYPTO frame at offset 0 whose bytes begin a well-formed TLS 1.3 ClientHello — handshake type 1, legacy_version 0x0303, a legacy_session_id of at most 32 bytes, a non-empty even-length cipher_suites list. The version must be one this milestone still supports, and the datagram must be padded to 1200 bytes. Nothing in the candidate's library is used to read it. |
| `reg-01-h3-get` | gate | T1 | The candidate's http3-client fetches a 64 KiB pseudorandom file from the candidate's http3-server over UDP loopback and writes it to stdout byte for byte: control streams, SETTINGS, a QPACK-encoded HEADERS frame and DATA, end to end. A request for a path that is not in the document root is answered rather than left to hang. |
| `reg-01-stream-transfer` | gate | T1 | The candidate's client fetches a 512 KiB pseudorandom file from the candidate's server over UDP loopback and writes it to stdout byte for byte. Large enough to cross the initial congestion window, the initial flow-control window and many packet boundaries, so a stack that reassembles out-of-order or retransmitted data incorrectly produces the wrong bytes rather than the right ones by luck. |
| `gate-endtoend` | gate | T1 | The candidate's workspace builds with cargo build at the root, producing working server and client binaries that complete a simple QUIC handshake and transfer bytes over a stream. |

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

`/app` contains everything step 02 left behind, unchanged. The milestone before this one is modelled on quiche 0.3.0; this one is modelled on 0.5.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

