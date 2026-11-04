# Specification notes — step 06

First-party prose stating the normative protocol and design facts this step is
graded against. Every fact is cited to a numbered section of the Tor
specifications at commit `1db24dddbf2203f2a465747302f133ef3ed6eed4`
(2022-11-28) with a line-anchored URL. No specification body text is
reproduced; the citations point at section headings and the prose below is
written for this workspace.

---

## 1. Bootstrap may be deferred

Steps 05 and earlier describe a client that bootstraps the moment it is
constructed. Nothing in the protocol requires that. Bootstrapping is a local
policy decision: a client may be constructed in an unbootstrapped state and
perform the network-facing work later, on the first request that needs it, or
only when the caller asks for it explicitly.

**tor-spec.txt §2 (Connections), L268:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L268

**dir-spec.txt §5 (Client operation), L3680:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3680

**dir-spec.txt §5.1 (Downloading network-status documents), L3685:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3685

Two behaviours follow and both are graded.

With **on-demand** bootstrap, an operation that needs the network triggers
bootstrapping itself, so a caller that never issues such an operation never pays
for the directory fetch. With **manual** bootstrap, no operation ever
bootstraps implicitly: the caller must ask, and until it does, every
network-facing operation fails in a distinguishable way rather than silently
starting a fetch the caller did not request.

The failure is a *classification*, not an opaque error: a caller that receives
it has to be able to tell "this client is not bootstrapped yet" apart from "the
network is unreachable" or "the address was rejected". This is the reason the
step introduces a shared error-kind vocabulary rather than one error type per
crate.

---

## 2. A shared error taxonomy

Every fallible operation in the client stack must report *what kind of thing
went wrong* in a way that is stable across crate boundaries and independent of
the concrete error type that carries it. The taxonomy is deliberately coarse:
it is for choosing a response, not for reproducing a stack trace.

The kinds that this step's groups depend on, and the concepts they name:

| kind | concept |
| --- | --- |
| `Internal` | an invariant of this program was violated |
| `BadApiUsage` | the caller used the API in a way the documentation forbids |
| `NotImplemented` | the peer or the protocol asked for something this build does not do |
| `BootstrapRequired` | the request needs network state the client does not have yet |
| `ForbiddenStreamTarget` | the requested destination is refused by policy |
| `InvalidStreamTarget` | the requested destination could not be formed |
| `LocalProtocolViolation` | the local peer spoke something this protocol does not allow |
| `TorProtocolViolation` | a Tor peer sent something the protocol does not allow |
| `TorAccessFailed` | a relay could not be reached over the network |
| `RemoteProtocolFailed` | a remote peer failed at a layer below Tor |
| `TorNetworkTimeout` | the network did not answer in time |
| `CircuitCollapse` | a circuit is gone |
| `TransientFailure` | the same operation may succeed if retried |
| `RemoteStreamError` | the far end reported a failure of its own |
| `PersistentStateAccessFailed` | the state directory could not be reached |
| `PersistentStateCorrupted` | the state on disk could not be read back |
| `CacheCorrupted` | cached directory material could not be used |
| `CacheAccessFailed` | the cache directory could not be reached |
| `DirectoryExpired` | directory material is out of date |
| `InvalidConfig` | a configuration value was rejected |
| `InvalidConfigTransition` | a value cannot be changed while the client runs |
| `ReactorShuttingDown` | the runtime is going away |

**tor-spec.txt §0.1 (Notation and encoding), L95:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L95

**dir-spec.txt §1.2 (Document meta-format), L260:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L260

A diagnostic that reaches a human must also state what happened. The error
types keep a `Display` that renders a readable sentence; the taxonomy is an
*addition* to that, never a replacement. Where an error wraps another error, the
classification of the inner error is what the outer one reports, unless the
outer layer has a more specific verdict of its own — for example a channel-level
I/O failure that the proto layer classes as a local network problem is
reclassified by the channel manager as "the relay is unreachable".

---

## 3. HTTP sent to a SOCKS port

A SOCKS proxy port and an HTTP proxy port are both "a port you point a browser
at", so users configure the wrong one routinely. A SOCKS handshake begins with a
version byte: SOCKS4 uses `4`, SOCKS5 uses `5`. An HTTP request begins with an
ASCII method name, whose first letter is one of `C` (CONNECT), `D` (DELETE),
`G` (GET), `H` (HEAD), `O` (OPTIONS), `P` (POST or PUT), or `T` (TRACE). The two
first-byte spaces are disjoint, so a SOCKS server can recognise an HTTP request
immediately without waiting for more input, and can say so in a way that names
the mistake.

**socks-extensions.txt §4 (HTTP-resistance), L92:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L92

**socks-extensions.txt §1 (Overview), L13:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L13

**socks-extensions.txt §1.1 (Extent of support), L31:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L31

Once the first byte has been rejected the handshake cannot continue: the
connection carried an HTTP request, not a SOCKS handshake, so every later byte
belongs to a protocol this state machine does not speak. Further input is
therefore an internal misuse rather than a second protocol violation.

---

## 4. A SOCKS handshake is a state machine over a byte stream

The proxy protocol is read from a stream that arrives in arbitrary chunks, so a
parser cannot assume it has seen all of the input it needs. The correct shape is
therefore a *two-level* result: the outer level says whether enough input has
arrived, and the inner level says whether the input that did arrive was
acceptable. Conflating the two makes a slow client indistinguishable from a
malicious one, and makes it impossible to resume after a short read.

**socks-extensions.txt §2 (Name lookup), L63:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L63

**socks-extensions.txt §3 (Other command extensions.), L81:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L81

**socks-extensions.txt §5 (Optimistic data), L100:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L100

Two consequences follow. One is graded: a handshake that returns "not enough
input" must remain usable — feeding the rest of the bytes to the same handshake
object must continue from where it stopped, not restart. The other is a
property of the server side rather than of this step's contract: a client may
legitimately send optimistic data before the handshake completes, so a server
implementation has to tolerate trailing bytes rather than treating them as a
protocol violation. **Trailing-byte tolerance is deliberately not graded here**
(`TEST_INTERFACE.md` §10 says so and gives the reason: it needs a live stream
to the exit).

---

## 5. Closing streams carries a reason

When one end of a stream is finished with it, the other end is told *why*. The
reason is a small integer with a fixed meaning, and the values are partitioned
into those a remote peer may send about its own stream and those that describe a
relay-or circuit-level condition. A client that does not recognise a value must
still be able to report it rather than discarding the stream silently, because a
future version of the protocol may add values.

**tor-spec.txt §6.3 (Closing streams), L1865:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1865

**tor-spec.txt §6 (Application connections and stream management), L1645:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1645

**tor-spec.txt §6.1 (Relay cells), L1647:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1647

The graded facts are the wire code points `1` through `14` for the reasons
defined at this commit, the partition of those values into stream-scoped and
circuit-scoped conditions, and the requirement that an unrecognised value is
still usable and is displayed by its number.

---

## 6. Channels, circuits and directory material fail differently

The link layer, the circuit layer and the directory layer each have failure
modes that a caller has to tell apart, and the step grades that each layer
classifies its own failures rather than collapsing them into one bucket.

**tor-spec.txt §2 (Connections), L268:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L268

**tor-spec.txt §4 (Negotiating and initializing connections), L603:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L603

**tor-spec.txt §5.3 (Creating circuits), L1378:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1378

**tor-spec.txt §5.4 (Tearing down circuits), L1474:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1474

**dir-spec.txt §5.5 (Retrying failed downloads), L3890:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3890

**dir-spec.txt §6.2 (HTTP status codes), L3993:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3993

Two rules are graded. A directory fetch that fails for *any* reason means the
circuit that carried it is suspect and should be retired. And a failure that
crosses a layer boundary keeps the inner classification unless the outer layer
has a strictly more specific verdict — a "no such relay in the consensus" is not
"the network is down", and neither is "the cache on disk is corrupt".

---

## 7. Unused channels expire

Channels cost memory and sockets at both ends, so a client reclaims one that has
carried no traffic for long enough. The needed lifetime is short at the end of a
burst and long in the middle of one, so the policy is to keep a *recently used*
channel and drop a *long idle* one; the graded facts are the shape of the API
(expiry returns the interval until the next sweep, and a channel can report how
long it has been unused) and the empty-manager case, which must be well defined
rather than a special error.

**tor-spec.txt §2 (Connections), L268:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L268

**tor-spec.txt §7.1 (Link throttling), L2002:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2002

---

## 8. Configuration sources, watching, and locked fields

A client's configuration comes from several places at once — compiled-in
defaults, one or more files, and command-line overrides — and the order in which
they are applied decides the effective value. Later sources win, and defaults
are the floor that always applies when nothing else sets a value.

**dir-spec.txt §1.2 (Document meta-format), L260:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L260

**tor-spec.txt §0.1 (Notation and encoding), L95:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L95

A file named in the configuration may be optional, in which case its absence is
not an error; a file named as required must exist, and its absence is. The
default listen port for the SOCKS proxy on this platform is `9150`, and the
default console log filter is `debug`.

Some settings can be changed while the client runs and some cannot. A request to
change a locked field is refused with a distinct classification from a plain
invalid value, and the refusal names the field so the caller can tell which one
was rejected; a refusal arising from a nested configuration section prefixes the
field name with the section, so the complete path is reported.

Watching a configuration file for changes is itself a setting, disabled by
default. It can be enabled from a file or from the command line, and the command
line wins; it can also be turned back off by a later file, so the flag is an
ordinary configuration value and not a one-way switch.

---

## 9. HTTP over Tor, and the TLS backend question

An HTTP client library is layered over the Tor client by giving it a connector:
a value that takes a URI and returns a stream. The connector must therefore
implement the universal service shape — poll for readiness, then produce a
future — and must fail *before* opening a socket on a URI it cannot serve, so
that a caller's retry logic does not mistake a malformed URI for a network
problem.

**dir-spec.txt §6.1 (HTTP headers), L3952:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3952

**dir-spec.txt §6.2 (HTTP status codes), L3993:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3993

**tor-spec.txt §2.1 (Picking TLS ciphersuites), L422:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L422

**tor-spec.txt §2.2 (TLS security considerations), L478:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L478

The connector is generic over the TLS implementation, because the protocol does
not fix which library performs the handshake: a build may offer more than one
backend and must satisfy the same runtime contract either way. This step grades
only that both backends still satisfy that contract — construct, clone, run a
future, spawn a task, compose with a timeout. Handshakes, certificates and
cipher suites are out of scope here and are covered at a later step.
