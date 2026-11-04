# Step 3 — draft-29, version contraction, and cubic congestion control

## 1. Implement this milestone

This milestone adds draft-28 and draft-29 while removing drafts 23, 24 and
25, leaving three supported versions: 27 (inherited from the previous step),
28 (new here) and 29 (new here). Read the arithmetic carefully: the previous
milestone supported four versions -- drafts 23, 24, 25 and 27 -- so draft-28
is work you have to do, not a version you are carrying forward. The Initial
salt and Retry integrity key are now functions of the QUIC version, with
draft-29 using distinct values. CUBIC becomes the default congestion control
algorithm with HyStart++ enabled for slow start.
Connection::stream_priority() appears in the connection API, matching the
urgency/incremental model RFC 9218 defines for HTTP/3 extensible priorities
-- the wire-level Priority header field, its u= and i parameters, and the
PRIORITY_UPDATE frame are not part of this release and arrive at 0.14.0
(step 7). Connection ID authentication and transport-integrated qlog also
land in this release. qlog is wired in behind the "qlog" Cargo feature --
the qlog data-model crate has sat unused at tools/qlog since the previous
step, and this milestone adds the optional dependency edge and the
Connection::set_qlog method that uses it -- and is exercised below. PTO
probe changes land as well but, like Connection ID authentication, are not
independently checked at this milestone -- see the required-to-exist section
of `TEST_INTERFACE.md` for why.

Three versions that were negotiable at the previous step—drafts 23, 24, and
25—are now unrecognised and must draw a Version Negotiation packet. The
Initial salt changes from a constant to a version-dependent value, affecting
key derivation for every observed Initial packet.

What is newly in scope here:

* `cap-draft29-salt` — A client Initial packet with wire version
  0xff00001d decrypts successfully using the draft-29 Initial salt
  (afbfec289993d24c9e9786f19c6111e04390a899), and fails when decrypted with
  the draft-27 salt.
* `cap-drafts-removed` — Client Initial packets with wire versions
  0xff000017 (draft-23), 0xff000018 (draft-24), and 0xff000019 (draft-25)
  each provoke a Version Negotiation packet listing only 0xff00001b,
  0xff00001c, and 0xff00001d, never a server Initial.

  The Version Negotiation packet must also **echo the client's connection
  IDs back swapped**, as RFC 9000 Section 6.1 requires: its Destination
  Connection ID is the client's Source Connection ID, and its Source
  Connection ID is the client's Destination Connection ID. This is inherited
  behaviour rather than new work at this milestone, but the case asserts it,
  so it is stated here rather than left for a candidate to infer.
* `cap-cubic-default` — A Connection created with Config::new and no
  explicit set_cc_algorithm_name call uses CUBIC. After a congestion event,
  cwnd is reduced by a factor of 0.7 (CUBIC's beta) rather than 0.5
  (Reno's). A fresh Config's set_cc_algorithm_name also rejects a name that
  is neither "reno" nor "cubic" with Error::CongestionControl.
* `cap-h3-priorities` — The h3-29 ALPN is read out of the http3-client's
  own ClientHello: the checker catches the client's first flight on a socket
  of its own, opens the Initial with its draft-29 keys and finds `h3-29`
  offered ahead of any older h3 name. The http3-server and http3-client
  binaries then complete an HTTP/3 GET against each other and the body
  arrives byte for byte.

  This requires that **draft-29 (0xff00001d) is the wire version the client
  sends by default**, with no flag needed to select it. All three of
  draft-27, draft-28 and draft-29 are supported at this milestone, but the
  checker opens that first flight with draft-29 keys alone: a client that
  defaults to one of the other two produces an Initial the checker cannot
  decrypt, and the group fails even though the version it chose is a
  supported one.
* `cap-retry-integrity-draft29` — A token-less draft-29 Initial sent to
  the candidate's server draws a Retry packet whose integrity tag verifies
  under the draft-29 Retry key and nonce (ccce187ed09a09d05728155a6cb96be1 /
  e54930f97f2136f0530a8c1c) over the Retry pseudo-packet built from the
  original destination connection ID, and which differs from the tag the
  pre-draft-29 key (4d32ecdb2a2133c841e4043df27d4430) computes over the
  identical pseudo-packet. The Retry carries a token and no inline ODCID.
* `cap-hystart-default` — A Connection created with Config::new has
  HyStart++ enabled. Holding the congestion control algorithm at Reno so
  that enable_hystart is the only thing varying, a connection whose
  round-trip time rises between slow-start rounds leaves slow start for
  Limited Slow Start before any packet is lost, and its congestion window
  after five rounds is several times smaller than that of an otherwise
  identical enable_hystart(false) connection given the same treatment. A
  Config::new connection tracks the enable_hystart(true) one.
* `cap-stream-priority-api` — Calling
  Connection::stream_priority(stream_id, urgency, incremental) on a live
  HTTP/3 request stream, on both ends, succeeds for every urgency from 0 to
  7 and both values of incremental, and the request still completes
  afterwards with its response headers and its whole body.
* `cap-qlog-wired` — A writer installed with Connection::set_qlog before
  the handshake starts has received some structured output by the time the
  handshake completes, and strictly more after a further round of
  application data crosses the connection -- ruling out both a stub that
  never writes and a stub that writes one snapshot at installation and
  nothing again. The bytes' internal schema (qlog's own JSON structure,
  event names) is not inspected; only that real, growing output reaches the
  writer this milestone's set_qlog is given.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 02 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9001.txt` — 5.2, 5.8. Initial Secrets (Section 5.2) and
  Retry Packet Integrity (Section 5.8): the same HKDF-Extract-with-salt and
  AEAD-pseudo-packet methodology this milestone reapplies with draft-29's
  own salt and Retry key/nonce. Those two constants are draft-29-specific
  and have no RFC of their own (only the final v1 values in this section are
  normative here), so both are given inline in the groups below instead.
* `/app/spec/rfc9218.txt` — 4, 4.1, 4.2. Priority Parameters, Urgency and
  Incremental: the semantic model Connection::stream_priority() implements
  locally at this milestone. The wire-level Priority header field (Section
  5) and PRIORITY_UPDATE frame (Section 7) this RFC also defines are not
  part of this release; see the required-to-exist section of
  `TEST_INTERFACE.md`.
* `/app/spec/rfc9002.txt` — Appendix B. The loss-detection and
  congestion-control pseudocode, and specifically the order it fixes for one
  acknowledgement: `DetectAndRemoveLostPackets`/`OnPacketsLost` before
  `OnPacketsAcked` (B.4), and no window growth while
  `InCongestionRecovery(acked_packet.time_sent)` holds (B.5). CUBIC's own
  arithmetic is not in this RFC, but the event path these groups measure
  `cwnd` through is, and a module that acks first and detects loss afterwards
  reports a different window at the moment it is read.
* `/app/spec/rfc9001-appendix-a.txt` — Appendix A. The worked TLS 1.3
  Initial-key example appendix-a-vectors.json decomposes into named byte
  strings; the checker replays the same file, so a disagreement with it is a
  disagreement with this appendix.

**Additional files:**

* `/app/TEST_INTERFACE.md` — the contract between your code and the
  checker: every name, path and flag it depends on, the group table it
  defines, and what is deliberately out of scope. Where it and your intuition
  disagree, it wins.
* `/app/WORKSPACE_HANDOFF.md` — what the previous step left behind and
  which of its milestones remain in scope here.
* `/app/spec/README.md` — which sections of which document are
  load-bearing for this step, and `SHA256SUMS` proving the corpus is
  byte-identical to the IETF originals.
* `/app/spec/appendix-a-vectors.json` — RFC 9001 Appendix A's worked
  example as named byte strings. The checker carries a byte-identical copy of
  the same file in its own harness; no group at this step reads either one, so
  it is a reference for your own plumbing rather than a check that runs.
* `/app/.cargo/config.toml` — `QUICHE_BSSL_PATH` points at the BoringSSL
  prebuilt into the image. Link it; do not fetch or compile one. The
  build must succeed with no network.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer; do not inspect `/tests`,
  later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `spec/`) or weaken, filter, rename,
  or disable tests. Run the references' local checks and leave a reproducible
  handoff.
- This milestone is modelled on a real crate. Depending on, vendoring, or
  copying source from `quiche`, `tokio-quiche`, `quinn`, `s2n-quic`, `neqo` or
  any other QUIC or HTTP/3 implementation is a task-specific answer and is out
  of bounds; the tree is built offline and a manifest that names one will not
  resolve. Cryptography, byte-buffer and async-runtime crates are ordinary
  dependencies and are fine.
