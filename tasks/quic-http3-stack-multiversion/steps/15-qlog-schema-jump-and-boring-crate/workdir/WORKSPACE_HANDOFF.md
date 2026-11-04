# Workspace handoff — step 15

You are continuing the workspace step 14 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

The previous step left an eleven-member workspace, a netlog reader, the HPACK
Huffman coder relocated into `octets` behind a feature, a retry-aware server
accept, and a connection generic over a buffer factory. All of it stays, and
the workspace keeps its eleven members under the same names.

Two things about that inheritance matter here. The `qlog` crate is a workspace
member, so the schema change is a change to a package the candidate owns rather
than to a dependency -- and the transport crate depends on it behind the
non-default `qlog` feature, which is where the event names it emits come from.
And the transport crate's TLS layer is already split into per-vendor modules
behind features, which is the seam the crypto backend change moves along.

`apps` is unchanged, and its two binaries are still named `quiche-client` and
`quiche-server`. They remain the only thing in the package that proves the
whole of it still works, and they are what the proof-of-life gate runs.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-04-datagram-roundtrip` — step 04's `cap-datagram-roundtrip`, re-run unchanged: The DATAGRAM extension negotiated, a 256-byte payload sent by the client and echoed by the server, both directions byte-for-byte, a received DATAGRAM opening no stream, and an empty queue answering `Done`. It is carried here because this step adds a second way to reach the same queues. `dgram_send_buf` and `dgram_recv_buf` are new entry points onto the send and receive queues that `dgram_send` and `dgram_recv` have used since milestone 04, and the obvious way to add them is to rewrite the queues to hold owned buffers and reimplement the copying pair on top. This group is the copying pair, unchanged, observed through the API it has always had. The behaviour this step deliberately changes is not in it: nothing here touches the field section default, the qlog schema, or the event names.
* `reg-09-key-update` — step 09's `cap-key-update`, narrowed to a single connection pair through one key update: A 1-RTT key update driven from the wire and verified with the verifier's own packet protection, which is written from RFC 9001 and does not depend on the candidate. It is carried here because this step replaces the crypto. BoringSSL stops being a vendored submodule compiled by the build script and becomes the `boring` and `boring-sys` packages from the registry, and the key schedule -- the secrets, the HKDF labels, the header protection, the phase bit -- runs through whichever one is linked. A second implementation reading the candidate's packets is the only observation that distinguishes "the new backend is wired up" from "the new backend is wired up and produces the same bytes". The observation is of the wire, not of the backend: nothing in it names a crate, a feature or a build script.
* `reg-01-h3-get` — step 01's `cap-h3-get`, re-run unchanged: The end-to-end HTTP/3 fetch, run between the candidate's own `quiche-server` and its own `quiche-client`, as two real processes over UDP loopback. The server is started on a loopback port serving a 64 KiB file the verifier wrote. The client fetches it with `--no-verify` and with `--http-version HTTP/3` pinned rather than left to the apps' own negotiation, exits 0 within the group's deadline, and the bytes on its stdout match the file byte-for-byte. Pinning the version is what keeps the inherited observation HTTP/3 and stops it quietly becoming a different group. This is the plan-mandated end-to-end regression, and it is the only group in the package that makes `apps` resolve its own features. The seed's `apps` manifest names no crypto backend at all -- it inherits the transport crate's default -- so a split-backend failure is not reachable here; what the group does enforce is that the whole workspace still builds against the registry BoringSSL and that the two binaries still talk to each other, which is where a half-converted workspace shows first. Ports are chosen free per case and the driver runs one case per `cargo test` invocation, so there is nothing to contend over; the harness's `serial_t1` guard is defined but called by no case at this step, and the serialisation the sentence above describes is the driver's, not a lock the case takes.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

