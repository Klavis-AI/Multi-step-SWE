# Test interface — step 03 (release 0.20.7)

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
-p rustls --example bogo_shim --features dangerous_configuration,quic
-p rustls-examples --bins
```

That is, literally:

```sh
cargo build --locked -p rustls --example bogo_shim --features dangerous_configuration,quic
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
-p rustls-mio --examples
```

The verifier takes the first that builds. That tolerance is there so packaging
is never what decides a step: where this release moves a crate, renames a
target or changes a feature set, making that move is part of the milestone and
your instruction describes it — but no group passes or fails on which of the
accepted specs produced the binaries, and §6 records that. What the line above
fixes is the shape the rest of this document assumes. If the tree you hand back
builds under none of the accepted specs, every group fails.

That is **not** the line the previous step was built with. The previous step's was:

```
-p rustls --example bogo_shim --features dangerous_configuration,quic
-p rustls-mio --examples
```

Reconciling the two is part of this milestone rather than an accident of packaging: the restructuring is upstream's, not the verifier's invention, and the rest of this document is written against the new line. Any route that gets there is accepted — what is fixed is the line, not how you satisfy it. The previous line stays accepted so that nothing here depends on packaging (§6); a tree that builds under neither fails every group.

Both attempts run with `RUSTFLAGS=--cap-lints=allow`, which is also the default
in this workspace. The tree you inherited denies lint sets chosen years before
this compiler existed, and several of them now fire on untouched upstream code;
capping them keeps a warning from ending the build. Nothing here checks
lints, so you do not need to silence them — but do not rely on a `deny` in the
crate root to catch mistakes for you, because it will not fire.

## 2. The interoperability shim

- The shim binary must exist at `/app/target/debug/examples/bogo_shim` after the build above.
- It must speak the **BoringSSL runner shim protocol** at pinned commit
  `3fea50a7cb55bb72e4a69d34015f1a6971e7934c`. That protocol is public; the runner's source is the normative
  description of it.
- The verifier stages its own frozen copy of the runner at `/app/bogo/bogo`
  (runner subdirectory `ssl/test/runner`) and its own `config.json` beside it. Your
  copy of either is ignored, so you cannot switch off a case you fail.
- The verifier **replaces `/app/test-ca`** with its own frozen PKI before
  running. Do not keep anything you need in that directory.
- The runner drives your shim over a pipe. It is invoked from its own
  directory as
  `runner.test -shim-path /app/target/debug/examples/bogo_shim -shim-config <config> -json-output <file> -pipe -allow-unimplemented -loose-errors -test.timeout 120m`,
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
  implementation would have picked.
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
- Some cases ask what your shim does with a resumption offer whose obfuscated
  ticket age is a fixed number of seconds away from the server's own reckoning,
  and the runner hands your shim that delay as `-resumption-delay`: when your
  shim is the server it rewinds the stored ticket's creation time by that many
  seconds, and when it is the client it rewinds the ticket epoch it holds by the
  same amount. The runner compares **whole seconds**, and two of the cases sit exactly on the boundary
  in each direction (60 seconds accepted, 61 refused). Keep your own clock
  arithmetic off that boundary: a shim that reads the clock on both sides of a
  second tick can report an age one second out and fail a boundary case without
  anything else being wrong. The reference aligns its read to just after a whole
  second before the handshake starts, and that is the whole of the discipline --
  nothing here depends on an age that is wrong by more than a tick.

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

## 4. What is observed over the wire

The verifier connects to your server with an independent TLS implementation,
points your client at one, and reads what crosses the socket. Nothing below the
socket is inspected: there is no hook into your code, no symbol is looked up,
and no internal state is read.

At this step, what it reads is:

- whether your server completes a handshake with an independent TLS implementation, and **which protocol version is negotiated**
- whether your client completes a handshake with an independent TLS implementation, what it writes to stdout, and its exit status
- whether your client **refuses** a certificate chain it cannot build a path for, by exiting non-zero
- how many NewSessionTicket messages your server sends on a single fresh TLS 1.3 connection, which the peer holds open after the handshake until a second has passed with no further ticket, and for no longer than twelve. At this step the count that passes is **exactly 1**: fewer fails, and so does more
- whether the **first record** your server sends back to a ClientHello whose `server_name` is **not a legal hostname** is a handshake record or an alert, and if an alert, its level. Such a hello must be **refused with a fatal alert as that first record**; answering it with a ServerHello fails, and so does closing the connection without sending anything. Only the level is read, never the description — which description fits an unparseable name is your choice. The names sent are `ab@cd.com` and `.`; every one of them must be refused
- whether a ClientHello delivered in several records, or split at an arbitrary byte boundary, is still read
- whether a ClientHello delivered with a **zero-length record** in front of it or in the middle of it is **refused** -- with a fatal alert, or by closing the connection -- rather than answered. Either refusal is accepted; answering the hello is not
- whether your client, pointed at a destination given as an **IP address literal** rather than a hostname, sends a ClientHello at all. Only its arrival is read -- not what it contains, and not whether the handshake that follows could complete
- whether a handshake message whose header declares a length larger than the buffer bound (the inherited bound is 65535 bytes, and no release on this ladder changes it) is **refused** -- with a fatal alert, or by closing the connection -- instead of leaving the peer waiting for bytes that will never arrive. Either refusal is accepted; silence is not

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
- **The `quic` feature is compiled, not exercised.** It is on the build line, so the code behind it has to compile and its public items have to exist, but no in-scope case drives a QUIC connection at this step.
- **Key logging is out of scope.** `SSLKEYLOGFILE` and the key-log hooks are part of the library's surface, and nothing here turns them on or reads what they write.
- **How many tickets your client keeps, and whether it uses one more than once, is out of scope.** The count that is read is the one your *server* sends; no probe here resumes a connection twice to see which ticket came back, and no group inspects a client-side store. RFC 8446 Appendix C.4 still says a ticket should be used once -- that is a correctness obligation, not a requirement of this step.
- **How you arrive at the ticket count is out of scope.** §4 names the number that must reach the wire; whether it is your library's default, a value you set, or a constant you wrote, and the name, type and location of any setting that would change it, are not looked at. Only the number of NewSessionTicket messages the peer counts is.
- **Nothing about the order or the byte-for-byte contents of your ClientHello's extensions is in scope** at this step. Send what the handshake needs; the layout is yours.
- **The default-off half of the 0-RTT capability is not read at this step.** Your server must not receive early data unless it is configured to, and that obligation stands -- but no check here starts your server with early data switched off and asks what it advertises or whether it would accept any. Every early-data case at this step drives the shim with early data enabled, and the example server is driven only for the observables §4 names. Nor does anything here fix how much undecryptable early data a server with early data switched off must tolerate before refusing: the cases that reach that state send a handful of bytes and expect the handshake to complete afterwards, and any permissive bound satisfies them.
- **How you pack your handshake flight into records is out of scope** -- one record or one per message, both are accepted.
- **The words in your error messages, and the shape of your internal error type, are out of scope.** Failures are read off the wire -- a handshake that does or does not complete, an alert that is or is not sent -- and off process exit status. Where the conformance runner would have compared error strings, it is run with `-loose-errors` so that a case which must fail must still fail, but the words it fails with are not compared. The alert you send to a peer is a different thing from the words you log locally, and §2 says where an alert is asserted.
- **Lints and warnings are out of scope.** The build caps them (§1); a `deny` in a crate root will not fire and will not matter either way.
- **Your own unit tests are not compiled or run.** Nothing in this workspace is checked by `cargo test`; keep or drop them as you like.
- **Performance, allocation counts and binary size are out of scope.** The only timing that matters is the per-case and per-probe limits in §5.

None of this is a promise about later steps. It is what this step does not look
at.
