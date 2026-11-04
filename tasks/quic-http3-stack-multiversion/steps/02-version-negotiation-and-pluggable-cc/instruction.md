# Step 2 — version negotiation, pluggable congestion control, and the Retry integrity tag

## 1. Implement this milestone

This milestone turns a single-version stack into a multi-version one. Four
QUIC versions are now accepted — drafts 23, 24, 25 and 27 — with draft-27 as
the default, and a client offering anything else draws a Version Negotiation
packet instead of a handshake. That packet advertises only three of the
four: draft-25 is accepted if a client offers it, but is deliberately not
one of the versions `negotiate_version` lists. Congestion control stops
being hard-wired: a `quiche::CongestionControlAlgorithm` enum — re-exported at
the crate root, `Reno` its only variant — and a `set_cc_algorithm_name` setter
select it by name, with Reno the default. Retry packets gain the AEAD integrity tag draft-25 introduced,
replacing the inline ODCID field that draft-23 carried. `Stats` grows a
`delivery_rate` field, and the HTTP/3 layer offers all four `h3-NN` ALPN
strings rather than only `h3-23`.

Four things that were true at step 01 stop being true here, and all four are
the kind a candidate leaves half-done.

The Retry packet changes shape. At step 01 (draft-23) it carried the
original destination connection ID inline as an explicit field and had no
integrity tag. `quiche::retry()` takes no version argument at this
milestone: a token-less Initial in any of the four supported versions draws
the same Retry, ending in a 16-byte AEAD tag instead, with no inline ODCID
field. There is no Initial version here that still draws step 01's
inline-ODCID shape -- that shape stopped being reachable the moment this
step's Retry path shipped. A stack that still writes the inline field
produces a Retry whose last 16 bytes are not a tag: the checker reads them as
one, the tag does not verify, and `cap-retry-integrity-tag` fails. Writing
*both* shapes is the case no group here can tell apart -- the bytes between
the source connection ID and the trailing tag are opaque to the checker, so a
hybrid parses as a longer token and every assertion still holds. Write the
tag-only packet and delete the inline field; nothing at this step will catch
you leaving it in.

The HTTP/3 headers event changes shape. At step 01,
`quiche::h3::Event::Headers` was a tuple variant, `Headers(Vec<Header>)`, and
step 01's contract said so. At this milestone it is a struct variant with two
named fields: `Headers { list: Vec<Header>, has_body: bool }`. The checker
matches `quiche::h3::Event::Headers { list, .. }`, so a tree that keeps the
tuple variant does not compile against the harness and every in-scope group
goes with it — this is the one change here on which the whole step depends,
not just one group. The rest of `h3` keeps step 01's shapes.

The HTTP/3 frame type `0x0E` (DUPLICATE_PUSH) is deleted. At step 01 it
parsed into a dedicated `Frame::DuplicatePush` variant; at this milestone it
is not a known type at all, so it must parse as an unknown frame and be
**ignored** rather than rejected — HTTP/3 requires unknown frame types to be
skipped, not treated as errors. Leaving the old variant in the enum keeps
the parser accepting a frame the version no longer defines.

The example binaries' command line changes too, and it is not optional
plumbing: step 01's `client`/`server`/`http3-client`/`http3-server` took
`docopt` flags (`--listen`, `--cert`, `--key`, `--root`, `--no-verify`, and
so on). From this step through step 05 those flags do not exist -- the crate
that parses them, `tools/apps`, is not a workspace member again until step
06. `server` and `http3-server` now take **no arguments**: they bind
`127.0.0.1:4433` and resolve `examples/cert.crt`, `examples/cert.key` and
`examples/root` relative to whatever directory they are started in, not the
crate root. `client` and `http3-client` take **exactly one** argument, a
URL, and nothing else. There is no `--no-verify` flag because there is
nothing to disable: certificate verification is unconditionally off. A
binary that still expects step 01's flags does not start under this
milestone's harness, and every group that spawns one fails for a reason that
has nothing to do with QUIC or HTTP/3.

What is newly in scope here:

* `cap-version-negotiation` — The checker sends a well-formed Initial
  carrying the unrecognised version 0x6b3343cf to the candidate's server.
  The server answers with a Version Negotiation packet: long-header form, a
  version field of zero, the client's source connection ID as its
  destination and the client's destination connection ID as its source, and
  a supported-version list containing exactly three entries, in this order:
  0xff00001b (draft-27), 0xff000018 (draft-24) and 0xff000017 (draft-23).
  draft-25 (0xff000019) is accepted by `version_is_supported` if a client
  offers it, but is deliberately not one of the three this list advertises —
  adding it is the most plausible wrong answer. The server must not answer
  with a server Initial.
* `cap-draft27-default` — A Config built with the crate's own default
  version produces a client Initial whose wire version is 0xff00001b, and
  that Initial's payload decrypts with the draft-27 Initial salt. A server
  offered a draft-27 Initial completes the handshake rather than
  negotiating, while the same server still completes a handshake against a
  draft-23 Initial — all four versions are live, draft-27 is merely
  preferred.
* `cap-retry-integrity-tag` — The checker sends a token-less Initial and
  the candidate's server answers with a Retry. The packet ends in a 16-byte
  AEAD_AES_128_GCM tag that verifies against the Retry pseudo-packet built
  from the checker's original destination connection ID, and it carries no
  inline ODCID field — the byte count between the source connection ID and
  the token accounts for the token alone. Replaying an Initial that carries
  the issued token then proceeds to a handshake instead of drawing a second
  Retry.
* `cap-pluggable-cc` — `Config::set_cc_algorithm_name("reno")` returns Ok
  and `set_cc_algorithm_name` on an unregistered name — "cubic", "bbr", "" —
  returns Err rather than silently falling back. `set_cc_algorithm` accepts
  `CongestionControlAlgorithm::Reno` directly. A connection built through
  either path, and one built with neither, all transfer 64 KiB intact, and
  their `cwnd` grows from the initial window under acknowledged data: the
  selector is wired to a real controller, not stored and ignored.
* `cap-delivery-rate-stats` — `Connection::stats()` returns a `Stats`
  carrying a `delivery_rate` field. After a sustained 256 KiB transfer over
  a link the harness paces, it is positive and within an order of magnitude
  of the bytes-acknowledged-over- elapsed-time the harness measures
  independently. The other five fields step 01 established are still present
  and still move.
* `cap-duplicate-push-removed` — An HTTP/3 frame whose type field is the
  varint 0x0E, carrying a one-byte payload, is parsed as an unknown frame
  and ignored: the parse succeeds, no dedicated DUPLICATE_PUSH variant is
  produced, no connection error is raised, and a following HEADERS frame on
  the same stream is still delivered. Step 01 parsed this type into a
  distinct variant; here the type is not defined and HTTP/3's rule for
  unknown types — skip it — applies.
* `cap-h3-27-alpn` — The candidate's `http3-client` and `http3-server`
  complete a GET over the `h3-27` ALPN, and the ALPN the server advertises
  is the four-entry length-prefixed list
  `\x05h3-27\x05h3-25\x05h3-24\x05h3-23`. A client restricted to `h3-23`
  alone still completes: the older protocols remain on offer rather than
  being replaced. A client offering only `h3-29` — the next name this
  library will grow into, not yet one of the four — selects nothing: the
  handshake does not settle on a shared application protocol.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 01 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 6, 17.2.1. Version Negotiation: when a server
  must send one, and the packet's exact layout — the zero version field, the
  echoed connection IDs, and the supported-version list
* `/app/spec/rfc9001.txt` — 5.8. Retry packet integrity: the
  AEAD_AES_128_GCM tag computed over the Retry pseudo-packet, which is what
  replaces draft-23's inline ODCID field. The *method* is the one this
  milestone uses; the key and nonce printed in this section are QUIC v1's
  and are not -- see the interface section for the draft-25/27 constants.
* `/app/spec/rfc9114.txt` — 7.2.8, 9. Section 9 states the general rule
  this milestone relies on: implementations MUST ignore unknown or
  unsupported values in extensible protocol elements, which is what makes
  the deleted DUPLICATE_PUSH type a skip rather than a rejection. Section
  7.2.8 is the adjacent, narrower mechanism — the reserved frame-type ladder
  used to exercise that same rule — and itself points back to Section 9; it
  does not state the general rule on its own.

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
