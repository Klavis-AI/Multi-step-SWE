# Test interface — step 13 (release 0.23.45)

This document is the contract. **Every path, build command, flag, binary name
and wire observable the verifier uses is named here, in §6, or in a document
under `docs/specs/`.** The verifier may combine them in ways you do not expect,
but it will not check anything that none of those three states. §6 is the other
half of that promise: it names what is deliberately *out of scope*, so that a
gap here can be read as "out of scope" rather than "not yet written down".

The verifier treats your workspace as a black box. It builds it, then talks to
it the way a peer or a user would: over real sockets, and through the
interoperability shim. It does not call your functions, import your modules,
or depend on any name that is not below.

**How this is checked.** The verifier runs a fixed set of independent checks,
each passing or failing as a whole. They are a mix of conformance cases driven
through the shim and probes that drive your example binaries over a socket.
They fall into two kinds.

**Gate checks** are proof of life — your workspace builds and the shim runs —
plus the behaviour earlier steps already established, one end-to-end path per
milestone. Every one of them must pass. They add no requirement of their own;
they are the floor, and a failure there means this step is not satisfied,
however much new work you finished.

**Requirement checks** are this step's own new capability, plus the peripheral
breadth around it. Every one of them is in scope, and every case within a
check must pass.

Two consequences worth stating plainly. A step at which you change nothing
satisfies none of the new work — inherited behaviour is the gate, and the gate
adds nothing. And a regression is not a proportional loss: breaking one thing
an earlier step established means this step is not satisfied, so run the
earlier steps' own checks before you consider yourself done.

The list of checks itself is not published, because publishing it would make
this document an answer key rather than a contract — but every binary, flag and
observable any check can reach is named in this document or in `docs/specs/`,
and §6 names what is deliberately left alone.

## 1. How your workspace is built

From `/app`, as an unprivileged user, the verifier runs `cargo build --locked`
once per line of this step's build line, in this order, and every line must
succeed:

```
-p bogo
-p rustls-examples --bins
```

That is, literally:

```sh
cargo build --locked -p bogo
cargo build --locked -p rustls-examples --bins
```

Each is attempted with `CARGO_NET_OFFLINE=true` first, using a registry cache
warmed from the dependency graph this step's milestone is expected to build
with -- which is not necessarily the graph the lockfile you inherited names. If
this step moves a dependency, the crates for the *new* graph are the ones
already cached and the old ones may not be. If that fails it is retried with
the network and without `--locked`. Either outcome is accepted and neither is
held against you. **Build the line above.** It is the one this step's milestone
is written against and the one the reference solution builds with.

A line also counts as built if it succeeds under the spec an **earlier** step
used. Those are, newest first:

```
-p rustls --example bogo_shim --no-default-features --features ring,tls12,logging,std
-p rustls --example bogo_shim
-p rustls --example bogo_shim --features dangerous_configuration,quic
-p rustls-mio --examples
```

The verifier takes the first that builds. That tolerance is there so packaging
is never what decides a step: where this release moves a crate, renames a
target or changes a feature set, making that move is part of the milestone and
your instruction describes it — but no group passes or fails on which of the
accepted specs produced the binaries, and §6 records that. What the line above
fixes is the shape the rest of this document assumes. If the tree you hand back
builds under none of the accepted specs, every group fails.

That is the same line the previous step was built with. The workspace you inherit builds under it already, so nothing here asks you to restructure the crates — and a rename that breaks every spec §1 accepts fails every group.

Both attempts run with `RUSTFLAGS=--cap-lints=allow`, which is also the default
in this workspace. The tree you inherited denies lint sets chosen years before
this compiler existed, and several of them now fire on untouched upstream code;
capping them keeps a warning from ending the build. Nothing here checks
lints, so you do not need to silence them — but do not rely on a `deny` in the
crate root to catch mistakes for you, because it will not fire.

## 2. The interoperability shim

- The shim binary must exist at `/app/target/debug/bogo` after the build above. The verifier also accepts it at the location an earlier step put it — `/app/target/debug/examples/bogo_shim` — and takes the first that is executable, for the same reason §1 accepts an earlier build spec.
- It must speak the **BoringSSL runner shim protocol** at pinned commit
  `9389a4af2c6c47a59ec39bfb51e4da2fe971d1bc`. That protocol is public; the runner's source is the normative
  description of it.
- The verifier stages its own frozen copy of the runner at `/app/bogo/bogo`
  (runner subdirectory `ssl/test/runner`) and its own `config.json` beside it. Your
  copy of either is ignored, so you cannot switch off a case you fail.
- The verifier **replaces `/app/test-ca`** with its own frozen PKI before
  running. Do not keep anything you need in that directory.
- The runner drives your shim over a pipe. It is invoked from its own
  directory as
  `runner.test -shim-path /app/target/debug/bogo -shim-config <config> -json-output <file> -pipe -allow-unimplemented -loose-errors -test.timeout 120m`,
  and it launches your shim once per case, passing that case's flags on the
  shim's command line.
- The runner passes its own environment through to the shim it launches, and
  the verifier sets **`BOGO_SHIM_PROVIDER=ring`** in it. Every in-scope case list
  in this package was measured against the **`ring`** provider, and where
  upstream makes the staged `config.json` conditional on the provider it is
  resolved for `ring` to match. Which cases upstream disables differs between
  providers, so this is not a detail of packaging. A shim that selects its
  provider at run time must honour that variable; a shim built against a single
  provider must be built against `ring`.
- **A case this step covers must PASS.** A shim can decline a case by exiting
  with the status the runner reserves for "unimplemented" — `89`, which the
  shipped shim names `BOGO_NACK` — and that produces a SKIP. A SKIP is *not*
  a pass: it fails the check that lists the case exactly as an outright failure
  would — with one exception, the next bullet. `-allow-unimplemented` does not
  buy the case back. All it does is stop the runner treating an unimplemented
  case as a reason to abandon the rest of the corpus, so that the run reaches
  the cases that follow. A runner flag you have not implemented is work, not
  an escape hatch.
- There is one exception and it is always written down: where this step's
  `instruction.md` says a capability has been **withdrawn**, declining the
  cases it names is the required answer. Silence about a capability never
  means it must be withdrawn.
- The runner also starts the shim once with `-is-handshaker-supported` to ask
  whether it implements the split-handshake mode. Print `No` and exit 0.
- The runner is invoked with `-loose-errors`, and the `ErrorMap` and
  `TestErrorMap` sections are stripped from the `config.json` the verifier
  stages. Together those mean: **a case that must fail must still fail, and
  everything else the case asserts — version, cipher suite, whether resumption
  happened, what was received — is still asserted, but the error string your
  implementation produces is not compared to anything.** You do not have to
  reproduce any other library's error vocabulary, and no case is measured on one.
  (The staged config keeps one table, `TestLocalErrorMap`, but every entry in it
  either states the alert a case expects -- the bullet below -- or empties a
  comparison rather than adding one.)
- That relaxation is about the words **you** produce. It does not extend to the
  alert you send: the runner is your peer, and where a case states which alert
  it expects to receive from you, that expectation is compared and
  `-loose-errors` has no effect on it. So "this must fail" sometimes means
  "this must fail *with the alert the specification names for this situation*",
  and the specification that names it is in `docs/specs/`. Where the alert is a
  choice the specifications leave open rather than one they state, the pin is
  relaxed instead: the case still has to fail, but with any fatal alert you
  choose — this document never asks you to guess which description another
  implementation would have picked. One situation does have its description
  named for it and is checked that way: a certificate that is corrupt or whose
  signatures do not verify is refused with `bad_certificate` (RFC 5246
  section 7.2.2, and the same descriptions in RFC 8446 section 6.2), which is
  what the four `GarbageCertificate-*` cases this step covers require.
- `DisabledTests` is left as upstream wrote it for this release. It lists cases
  the runner is told not to run at all. It is not a list of what your
  implementation need not do, and the verifier does not consult it when
  deciding what is in scope.
- There is no per-case wall-clock limit. The runner as a whole is bounded by
  what is left of the step's budget, and `-test.timeout` is set above that so
  that the outer bound is the one that applies; what protects a single case is
  the runner's `-idle-timeout`, left at its default of 15 seconds per read or
  write to your shim. A shim that answers but answers slowly is not failed for
  it; a shim that stops answering is.

Some checks at this step start the shim **directly** rather than through the
runner. It is the same binary under the same protocol, launched with flags the
runner itself uses -- `-port`, `-server`, `-cert-file`, `-key-file`,
`-max-version` and `-require-any-client-certificate` -- and what is read is the
bytes it puts on the socket. Nothing is called, imported or inspected; a shim
that behaves correctly under the runner behaves correctly here.

One flag is read closely enough to be worth writing down in full.
`-use-client-ca-list` tells a server which issuers it should name as acceptable
when it asks for a client certificate. The runner passes it as a single
argument in one of three forms, and all three have to be accepted:

- `<NULL>` and `<EMPTY>` both mean **the empty list** -- no issuer is named.
  They are sentinels, not names; a shim that treats either as a literal
  distinguished name to be encoded is wrong.
- Anything else is a **comma-separated list of hex-encoded DER** distinguished
  names, each one the DER of a `Name` as `certificate_authorities` carries it,
  with no separator inside a name. A list of one has no comma.

Whatever it is given has to reach the wire as the acceptable-issuer list of
your `CertificateRequest`, in the order given. Declining the flag as
unimplemented fails the checks that pass it -- §2's rule that a SKIP is not
a pass applies to a probe exactly as it applies to a case.


## BoGo group families

The seven new-capability BoGo groups at this step all pass the 0.23.45 shim but not the 0.23.31 shim. The discriminating mechanism for 372 of the 381 cases, plus the two `RejectEmptyOCSPResponse-*` cases that are the same mechanism seen from the peer (`379` of the 381 in all; the other two are named in the certificate bullet below), is the OCSP guard added in 0.23.45: `with_single_cert_with_ocsp` now checks `if !ocsp.is_empty()` (backed by `PayloadU24<NonEmpty>` in `msgs/handshake.rs`) before setting `certified_key.ocsp` and emitting `CertificateStatus`; the 0.23.31 shim emits an empty `CertificateStatus` that the BoGo test client rejects with 'tls: error decoding CertificateStatus message'. An additional four cases exercise certificate-compression support added in 0.23.45 (`CertCompressionExpands-TLS13`, `CertCompressionShrinks-TLS13`, `CertCompressionRandom-TLS13`, `CertCompressionPriority-TLS13`). Three cases in bogo-tls13core-new gate on key-share and legacy-connect handling (`KeyShareWithServerHint-TLS13`, `KeyShareWithServerHint-UnrecognizedGroupIgnored-TLS13`, `NoLegacyServerConnect-TLS13`).

- **bogo-certificates-new** (79 cases) -- server certificate path: server-side signature-algorithm verification (`Server-Verify-*`, `Server-VerifyDefault-*`), client authentication (`ClientAuth-NoCertificate-*`, `ServerAuth-*`), certificate-authorities list handling (`TLS12-Server-CertReq-CA-List`, `TLS13-Server-CertReq-CA-List`), certificate compression (`CertCompression*`), OCSP rejection from the peer (`RejectEmptyOCSPResponse-TLS-TLS12`, `RejectEmptyOCSPResponse-TLS-TLS13`), and trailing-data / wrong-message-type rejection for certificate messages. Cases fail 0.23.31 via the OCSP guard path or the cert-compression path.

- **bogo-earlydata-new** (15 cases) -- 0-RTT early data server paths: basic server (`EarlyData-Server-TLS13`), ticket and HRR interactions (`EarlyData-FirstTicket-*`, `EarlyData-HRR-*`, `EarlyData-RejectTicket-*`), unexpected handshake (`EarlyData-UnexpectedHandshake-Server-TLS13`), and EndOfEarlyData trailing-data/wrong-message-type cases. Cases fail 0.23.31 because the server handshake that precedes early data triggers the OCSP guard path.

- **bogo-interop-new** (96 cases) -- TLS 1.2 basic interop: ALPN at TLS 1.2 (`ALPNServer-*-TLS12`), basic server handshakes (`Basic-Server-*-TLS`) across key types and handshake styles, client authentication at TLS 1.2 (`ClientAuth-Server-TLS-*`), server signature algorithm selection (`Server-Sign-*-TLS12`, `Server-SignDefault-*-TLS12`), version negotiation (`VersionNegotiation-Server*-TLS12-*`), and extended master secret. All fail 0.23.31 via the OCSP guard path during the TLS 1.2 Certificate handshake.

- **bogo-keyupdate-new** (2 cases) -- post-handshake key update: `KeyUpdate-FromServer` and `KeyUpdate-ToServer`. Fail 0.23.31 via the OCSP guard path on the preceding full handshake.

- **bogo-resumption-new** (66 cases) -- session resumption: no-ticket variants (`Basic-Server-NoTickets-*`, `SessionTicketsDisabled-*`), PSK binder validation (`Resume-Server-*Binder*`), ticket age skew (`TLS13-TicketAgeSkew-*`), cross-version resume (`Resume-Server-TLS12-*`, `Resume-Server-TLS13-*`), and session-ticket disable paths. Fail 0.23.31 via the OCSP guard path on the initial full handshake.

- **bogo-robustness-new** (20 cases) -- record-layer robustness: `SplitHandshakeRecords` variants of basic and client-auth handshakes (`Basic-Server-*-SplitHandshakeRecords`, `ClientAuth-Server-*-SplitHandshakeRecords`), plus `FragmentedClientVersion`, `LooseInitialRecordVersion-TLS12`, `MaxSendFragment-TLS12`, and trailing-data / wrong-message-type cases for TLS 1.2 ClientFinished and ClientKeyExchange. Fail 0.23.31 via the OCSP guard path.

- **bogo-tls13core-new** (103 cases) -- TLS 1.3 core: basic 1-RTT handshakes (`TLS13-1RTT-Server-*`) and HelloRetryRequest (`TLS13-HelloRetryRequest-Server-*`) across all handshake styles, ALPN at TLS 1.3, client authentication at TLS 1.3 (`ClientAuth-Server-TLS13-*`), curve negotiation (`CurveTest-*`), server signature selection at TLS 1.3 (`Server-Sign-*-TLS13`, `Server-SignDefault-*-TLS13`), version negotiation at TLS 1.3, and three cases gated on the key-share/legacy-connect change: `KeyShareWithServerHint-TLS13`, `KeyShareWithServerHint-UnrecognizedGroupIgnored-TLS13`, `NoLegacyServerConnect-TLS13`. All others fail 0.23.31 via the OCSP guard path.

Some of those cases pass flags on your shim's command line that are new to this
window and worth naming here, because the runner's source is otherwise the only
place they appear: `-server-supported-groups-hint <decimal group id>`,
`-expect-no-hrr` (no argument), and `-no-legacy-server-connect` (no argument).
They drive the three key-share and legacy-connect cases named above. A shim that
does not accept one of them fails the cases that pass it, and §2's rule that a
declined case is not a pass applies as it does everywhere else.

## 3. The example client and server

The verifier searches for your example binaries in this order and takes the
first one that exists and is executable, relative to `/app`. The two roles are
searched independently, so they need not be in the same place:

| # | server | client |
| ---: | --- | --- |
| 1 | `target/debug/tlsserver-mio` | `target/debug/tlsclient-mio` |
| 2 | `target/debug/examples/tlsserver-mio` | `target/debug/examples/tlsclient-mio` |
| 3 | `target/debug/tlsserver` | `target/debug/tlsclient` |
| 4 | `target/debug/examples/tlsserver` | `target/debug/examples/tlsclient` |
| 5 | `target/release/tlsserver-mio` | `target/release/tlsclient-mio` |
| 6 | `target/release/examples/tlsserver-mio` | `target/release/examples/tlsclient-mio` |
| 7 | `target/release/tlsserver` | `target/release/tlsclient` |
| 8 | `target/release/examples/tlsserver` | `target/release/examples/tlsclient` |

Upstream moved these examples between crates and renamed them more than once
along this ladder, so any entry on that list is accepted. If your binary is not
on it, the verifier will not find it.

They are invoked with **long-form flags only** — a short `-p` is not used,
because the argument parser changes partway up the ladder:

```sh
<server> --certs <chain.pem> --key <key.pem> --port <n> echo
<client> --cafile <ca.pem> --port <n> --http localhost
```

- The server must serve TLS on `--port` and, in `echo` mode, echo back what it
  receives.
- `--certs` is given a **PEM file holding the whole chain, leaf first**: the
  end-entity certificate, then the intermediates that lead to the root. `--key`
  is given the matching private key as PEM; it may be PKCS#1 (`BEGIN RSA
  PRIVATE KEY`) or PKCS#8 (`BEGIN PRIVATE KEY`), and accepting both is the safe
  choice.
- The peer that connects to your server **presents SNI `testserver.com`**, which
  is a name on the fixture certificate. A server that serves its one configured
  chain regardless of the name is fine; a server that requires a name-keyed
  lookup to have been configured is not.
- That peer offers both TLS 1.2 and TLS 1.3 and lets your server choose.
  **The handshake must come out at TLS 1.3.** Preferring the highest version
  both sides support is RFC 8446 §4.1.1 and §4.2.1; here it is also checked.
- The client must connect to `--port` on the name given as the final argument,
  complete a handshake, and in `--http` mode send an HTTP/1 request and write
  the response to stdout.
- **The client's exit status is read as its verdict**: zero when the handshake
  completed and the exchange succeeded, non-zero when it did not. It is pointed
  at a server whose chain cannot be verified as well as at one whose chain can,
  and returning zero for both is the same as verifying nothing.
- Both probes talk to your binaries over **`127.0.0.1`**. Binding the wildcard
  address is fine; binding something that is not reachable there is not.

Some checks read what your client offers under a stated *configuration* rather
than under its default. Where that happens the same binary is run again with
one extra long-form flag:

```sh
<client> --cafile <ca.pem> --port <n> --protover 1.2 --http localhost
```

`--protover` takes one version number and means "offer only this version".
A client that does not understand the flag, or that ignores it and offers
TLS 1.3 anyway, is answering a different question from the one asked.

The example **server** is run a second time as well, with one extra long-form
flag, to ask what it does when early data is switched on:

```sh
<server> --certs <chain.pem> --key <key.pem> --port <n> --max-early-data 2048 echo
```

`--max-early-data` takes a byte count and means "be willing to read up to this
many bytes of 0-RTT early data on a resumed connection". Two things follow. A
server that does not recognise the flag does not start, and the check that
drives it fails on that alone. And the number above is the one the verifier
passes and the one it expects to see advertised back: it is not a constant of
the protocol, and it is not the value this step's conformance shim configures
for its own cases, so a tree that hardcodes a limit rather than using the
configured one is caught here.

Two further example binaries are driven at this step, found by **name** rather
than by role. Each is searched for in this order, relative to `/app`, with
`<name>` replaced by the binary's name, and the first that exists and is
executable is taken:

| # | path |
| ---: | --- |
| 1 | `target/debug/<name>` |
| 2 | `target/debug/examples/<name>` |
| 3 | `target/release/<name>` |
| 4 | `target/release/examples/<name>` |

If a binary is not on that list under the name below, the verifier will not
find it, and the check that drives it fails.

`unbuffered-server` — the worked example for the unbuffered connection API:

```sh
<unbuffered-server> <chain.pem> <key.pem>
```

Two positional arguments, in that order: the PEM chain and the matching
private key, the same two files §3 describes above. It listens on port
**1443** — a fixed port, not one passed to it — and answers an HTTP/1 request
per connection.

`server_acceptor` — the worked example for the `Acceptor` API:

```sh
<server_acceptor> --port <n>
```

One long-form flag. It is started in a scratch directory of the verifier's own
and any files it needs it may write there; every other path it takes is left
at its default, so those defaults must be relative to the working directory
rather than absolute.

## 4. What is observed over the wire

The verifier connects to your server with an independent TLS implementation,
points your client at one, and reads what crosses the socket. Nothing below the
socket is inspected: there is no hook into your code, no symbol is looked up,
and no internal state is read.

At this step, what it reads is:

- whether your server completes a handshake with an independent TLS implementation, and **which protocol version is negotiated**
- whether your client completes a handshake with an independent TLS implementation, what it writes to stdout, and its exit status
- whether your client completes a handshake with a peer it was given an **IP address** as the name of, when the certificate that peer serves carries that address as an `iPAddress` subjectAltName -- and whether it **refuses** the same peer when the certificate names only DNS hosts
- whether your client **refuses** a certificate chain it cannot build a path for, by exiting non-zero
- how many NewSessionTicket messages your server sends on a single fresh TLS 1.3 connection, which the peer holds open after the handshake until a second has passed with no further ticket, and for no longer than twelve. At this step the count that passes is **exactly 2**: fewer fails, and so does more
- whether the **first record** your server sends back to a ClientHello whose `server_name` is **not a legal hostname** is a handshake record or an alert, and if an alert, its level. Such a hello must be **refused with a fatal alert as that first record**; answering it with a ServerHello fails, and so does closing the connection without sending anything. Only the level is read, never the description — which description fits an unparseable name is your choice. The names sent are `ab@cd.com` and `.`; every one of them must be refused
- whether the **first record** your server sends back to a ClientHello whose `server_name` is one of `127.0.0.1` or `::1` is a handshake record or an alert. Each of those must be **answered** — the first record back is a handshake record. A server that refuses every name it cannot use as a lookup key fails here. Each name is sent on a hello of its own. Every name in that list is an **IP address literal**, and that is the whole point of the check, so read the next three sentences before you implement it. RFC 6066 §3 says literal addresses “are not permitted” in `HostName`, and the reading of that sentence which refuses such a hello is a defensible one — it is what this library used to do, and it is what you will do if you implement the RFC and stop there. This release does not: it recognises the value as an address, records that the hello carried no usable hostname, and carries on with the handshake, because refusing broke peers that send one. **That deviation is what is checked here**, and it is in scope because it is this release's behaviour rather than because it is what the RFC asks for. Note what is *not* being asked: the value is not used for anything afterwards, nothing here selects a certificate by an address, and a server that stores it and then ignores it is correct
- whether the ServerHello your server sends back carries a `server_name` extension of its own -- the acknowledgement RFC 6066 section 3 requires from a server that based its choice on the name. Its *presence* is the whole question; it has no body to read. A hostname the server can use must come back **acknowledged**; an IP literal must come back **unacknowledged**, which is the same answer a hello carrying no `server_name` at all gets, and that baseline is sent too. This is the one probe in the set that negotiates **TLS 1.2**: under TLS 1.3 the same acknowledgement moves into EncryptedExtensions, where an observer holding no key cannot read it. The hello is an ordinary TLS 1.2 ClientHello offering ECDHE-RSA; nothing past the ServerHello is read, so no key exchange has to complete and no cipher suite preference of yours is being checked
- whether a ClientHello delivered in several records, or split at an arbitrary byte boundary, is still read
- whether a record carrying a ClientHello **followed by a second handshake message** is refused, and it must be refused **with a fatal alert** -- closing the connection without one fails, because RFC 8446 section 5.1 makes the alert the required answer to a message that does not align with the record boundary
- whether that refusal still holds when the ClientHello is **reassembled from several records** and the trailing handshake message rides in the record that completes it -- the same fatal alert is required -- and the control leg of the same check: the reassembled hello **without** a trailing message must still be answered
- whether the first record your server sends back to a ClientHello that offers `pre_shared_key` **without** `psk_key_exchange_modes` is a handshake record or a fatal alert. Only the level of the alert is read, never its description
- the length of the `legacy_session_id` field in the ClientHello your client sends when it is configured for TLS 1.2 only (the `--protover 1.2` invocation in §3) -- the passing value is **empty**; a non-empty `legacy_session_id` (e.g. the 32-byte TLS 1.3 compatibility mode value) fails
- whether a ClientHello delivered with a **zero-length record** in front of it or in the middle of it is **refused** -- with a fatal alert, or by closing the connection -- rather than answered. Either refusal is accepted; answering the hello is not
- whether your client, pointed at a destination given as an **IP address literal** rather than a hostname, sends a ClientHello at all. Only its arrival is read -- not what it contains, and not whether the handshake that follows could complete
- whether a handshake message whose header declares a length larger than the buffer bound (the inherited bound is 65535 bytes, and no release on this ladder changes it) is **refused** -- with a fatal alert, or by closing the connection -- instead of leaving the peer waiting for bytes that will never arrive. Either refusal is accepted; silence is not
- the content types of the records in your server's first flight, and how many of them carry the encrypted part. At this step the answer that passes is **exactly one** record carrying the encrypted part: the encrypted handshake flight arrives coalesced, and a server that spreads it over several records fails
- what your server advertises as its **0-RTT early-data limit**, and whether it reads early data offered under that limit. This is read from an ordinary client, and your server is started twice. Under the plain `echo` command line of §3, every NewSessionTicket it sends must advertise `max_early_data_size` **0** — no `early_data` extension at all and one whose body is zero are the same thing to the peer, and either passes — and a client resuming one of those tickets must still complete the handshake. Under the extra `--max-early-data` flag §3 names, every ticket must advertise **exactly the number that flag was given**, which at this step is **2048**, and a client resuming that ticket with early data in its first flight must have that data **accepted**. The two halves are one check between them: off unless switched on, and exactly the configured limit once it is. A server that does not understand the flag does not start and fails this outright. What happens when a client sends **more** than the limit is not read by this probe — §6 says where it is read and why it is not read here
- whether the ClientHello your client sends offers **extension 18 (`signed_certificate_timestamp`)**. Its *presence* is the whole question: what it would contain and where in the list it would sit are not read. Six connections are made under the default invocation in §3 and it must be offered in **none** of them
- the named groups your ClientHello offers, which of them carry a key share, and in what order. At this step the order that passes is the one this release's default produces and step 12 established: **`X25519MLKEM768` first in `supported_groups`, carrying a key share of its own**. It is re-read here rather than newly required -- the tree you inherit already has it -- and a hello that offers the group without preferring it, or prefers it without spending a share on it, does not pass
- the **bytes of the key share** your ClientHello spends on the hybrid group -- not just that the entry is there. Two things are read: its length, and whether the ML-KEM-768 half decodes as an encapsulation key, meaning the twelve-bit packed encoding with every coefficient reduced. The length and the order of the two halves are both fixed by §4.1 of that group's definition in `docs/specs/`, and the reverse order is the same number of bytes, so a share with the halves swapped is read as wrong rather than as the right size. Nothing is decrypted and no key agreement is performed: the X25519 half is not decoded at all, and what is read is a bounds test on a public encoding. This is not an extra connection -- it is read from the same hellos as the line above
- whether your **server can use `X25519MLKEM768`** rather than only list it. It is sent a ClientHello offering exactly that one group and carrying an **empty `client_shares`** -- legal, and per RFC 8446 §4.2.8 precisely how a client asks to be told what to send. Two answers are then available to it and what is read is which one comes back: a **HelloRetryRequest naming that group**, or a fatal alert. At this step the answer that passes is **the HelloRetryRequest**. No cryptography is performed by the probe and no handshake is completed; only the level of an alert is read, never its description
- whether the **unbuffered example server** named in §3 serves TLS at all: an independent TLS implementation connects to it, and what is read is that the handshake completes, that the version negotiated is **TLS 1.3**, and that an HTTP/1 request written after the handshake is answered with a response. Nothing about the API behind it is inspected — only that a server built on it works
- whether the **acceptor example server** named in §3 reports a refusal to its peer: it is sent a ClientHello whose `server_name` is not a legal hostname, and the **first record** it sends back must be a **fatal alert**. Closing the connection without a record fails. Only the level of the alert is read, never its description
- what the `CertificateRequest` your **shim** sends as a TLS 1.3 server asking for a client certificate says about acceptable issuers, **with no issuer hint list configured at all** -- no new runner flag is passed, so this is a configuration every release on this ladder can be driven into. The shim of §2 is started directly -- not through the runner -- with `-server`, `-port`, `-cert-file`, `-key-file`, `-max-version` and `-require-any-client-certificate`, and the `CertificateRequest` it sends is read off the socket; at TLS 1.3 it is decrypted first by an independent implementation completing the handshake. At this step the answer that passes is **no `certificate_authorities` extension at all** in the `CertificateRequest`. An extension carrying a zero-length list is a different answer and fails
- whether an issuer hint list **configured on your shim reaches the wire unchanged** at TLS 1.2. Two connections are made, both required, because either one alone is passable by a server that ignores the configuration entirely. Given a single hex-encoded DER distinguished name through `-use-client-ca-list`, the `CertificateRequest` must name **exactly that one and no other**. Given `<NULL>` -- the explicitly empty list -- it must name nobody, which at this version means a `certificate_authorities` **field of zero length**. The field is mandatory at this version, so leaving it out is not an available answer and would not parse. The name used appears nowhere in the fixture PKI, so it cannot be produced by echoing something the shim had from elsewhere
- whether an issuer hint list **configured on your shim reaches the wire unchanged** at TLS 1.3. Two connections are made, both required, because either one alone is passable by a server that ignores the configuration entirely. Given a single hex-encoded DER distinguished name through `-use-client-ca-list`, the `CertificateRequest` must name **exactly that one and no other**. Given `<NULL>` -- the explicitly empty list -- it must name nobody, which at this version means **no `certificate_authorities` extension at all** in the `CertificateRequest`. An extension carrying a zero-length list is a different answer and fails. The name used appears nowhere in the fixture PKI, so it cannot be produced by echoing something the shim had from elsewhere

Anything else the probes above see is free. That freedom is about **these**
probes: the conformance runner of §2 is also a peer of yours, and the bullets
there say what it compares -- including the one about the alert you send, which
is the easiest of them to read past. §6 names the observables that were
measured here and deliberately left out of scope.

## 5. Timing

Each step's budget is reported by `bash /app/timer.sh`. The configured timeout is
authoritative if they disagree. Individual probes are given 30 seconds each; a
server that needs longer than that to answer a handshake is treated as not
answering. The conformance runner is bounded as a whole by what is left of the
step's budget, and within it each read or write to your shim is bounded by the
runner's own idle timeout, which is left at its default of 15 seconds. If less
than five minutes of the budget remain when that stage would start, it is
skipped and each of its groups is recorded as a failure with that reason --
nothing a candidate does influences the budget, and the group names carry the
outcome either way.

## 6. Out of scope

Stated so that you can stop working on it, and so that silence elsewhere in
this document is not read as a hidden requirement.

- **Which accepted build spec produces the binaries is out of scope, and neither is where the shim ends up.** §1 lists the specs this step accepts and §2 the shim locations; the verifier takes the first that works and records nothing about the choice. Moving to this release's crate layout is part of the milestone your instruction describes, but it is the behaviour behind the binaries that is checked, never the packaging that emitted them. Proof of life asks only that the workspace builds and the shim runs.
- **Key logging is out of scope.** `SSLKEYLOGFILE` and the key-log hooks are part of the library's surface, and nothing here turns them on or reads what they write.
- **The client side of the encryption-level alignment fix is out of scope.** GHSA-2mjx-qc3c-rqvc is role-symmetric -- your client must also refuse a record that carries a handshake message after the one that changes read keys -- but no group here checks it. The crypto-free way to provoke it, a HelloRetryRequest packed with a trailing handshake message, is refused by both this release and the one you inherit; they differ only in the internal error each raises, and this section already says error words are out of scope. The record the upstream regression test uses instead is an EncryptedExtensions message re-encrypted under the server handshake secret, which a checker holding no key cannot build. Implement it -- it is the same one-line check as the server side -- but the server-side groups are what this step measures here.
- **How many tickets your client keeps, and whether it uses one more than once, is out of scope.** The count that is read is the one your *server* sends; no probe here resumes a connection twice to see which ticket came back, and no group inspects a client-side store. RFC 8446 Appendix C.4 still says a ticket should be used once -- that is a correctness obligation, not a requirement of this step.
- **How you arrive at the ticket count is out of scope.** §4 names the number that must reach the wire; whether it is your library's default, a value you set, or a constant you wrote, and the name, type and location of any setting that would change it, are not looked at. Only the number of NewSessionTicket messages the peer counts is.
- **What your server does when a client sends _more_ early data than it advertised is not read by the probe §4 describes — and it *is* read by the conformance runner of §2.** Both halves of that matter. The probe reads the limit out of your NewSessionTickets and checks that early data arriving under it is accepted; it cannot ask about an overrun, because the independent client it is built on enforces the advertised limit on its own side and refuses to write past it, so an over-cap connection through it would be measuring that client rather than your server. The runner's shim has no such scruple and cases that overrun deliberately are in this step's in-scope set. So the behaviour is in scope, and a tree that enforces the cap only for the exact byte count §3 names is found out there rather than here.
- **Nothing about the shape of your ClientHello is in scope except what §4 lists.** §4 names what is read -- the extensions offered and which of them carry a key share, the bytes of the hybrid group's key share where §4 says so, extension 18's presence, and the lengths §4 names. The order of the extensions *themselves* is free -- no particular arrangement is expected, and two hellos may disagree -- with the one exception §4 states: the key-exchange group list, whose first entry is the hybrid group. The bytes inside any extension §4 does not name are not read at all.
- **The words in your error messages, and the shape of your internal error type, are out of scope.** Failures are read off the wire -- a handshake that does or does not complete, an alert that is or is not sent -- and off process exit status. Where the conformance runner would have compared error strings, it is run with `-loose-errors` so that a case which must fail must still fail, but the words it fails with are not compared. The alert you send to a peer is a different thing from the words you log locally, and §2 says where an alert is asserted.
- **Lints and warnings are out of scope.** The build caps them (§1); a `deny` in a crate root will not fire and will not matter either way.
- **Your own unit tests are not compiled or run.** Nothing in this workspace is checked by `cargo test`; keep or drop them as you like.
- **Performance, allocation counts and binary size are out of scope.** The only timing that matters is the per-case and per-probe limits in §5.

None of this is a promise about later steps. It is what this step does not look
at.
