# Workspace handoff — step 14

You are continuing the workspace step 13 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

The previous step left a nine-member workspace with an asynchronous front end
on top of it, a `Connection` generic over a buffer factory, a fifth congestion
controller, and zero-copy sends at both the transport and the HTTP/3 layer. All
of it stays.

Two things about that inheritance matter here. The workspace root now carries
`[workspace.package]` and `[workspace.dependencies]`, so a new member is added
by writing a manifest that inherits from them rather than one that repeats
versions -- both of this step's new members do that, and one of them takes a
dependency from a git revision rather than from a registry. And `Connection` is
`Connection<F>`, so the new server-side constructor is generic in the same way
its neighbours are.

`apps` is unchanged, and its two binaries are still named `quiche-client` and
`quiche-server`. They remain the only thing in the package that proves the
whole of it still works, and they are what the proof-of-life gate runs.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-01-h3-static-table` — step 01's `cap-h3-static-table`, narrowed to the request direction: Carried because this step moves the Huffman coder out of the crate that owns the QPACK implementation. A request and a response are exchanged over an in-memory HTTP/3 pair and every field arrives intact, indexed and literal alike, **in order**. The request is the four pseudo-headers, all of them QPACK static table entries. The response mixes one static entry, `:status: 200`, with two literal fields, one of which carries a non-ASCII value. The literals are the point. A static-table entry is an index on the wire and never reaches the Huffman coder; a literal does, because the encoder writes the shorter of the raw and coded forms and the decoder has to invert whichever it finds. This step took that coder out of the transport crate and put it in `octets` behind a non-default feature, so the failure this group exists to catch is a candidate that relocates the code correctly and forgets to turn the feature on for the transport crate: the build succeeds, the static-table fields still arrive, and every literal comes back wrong or not at all. Order is asserted as well as content. QPACK may choose its representation per field but may not reorder a header section, and a decoder that emitted the indexed fields first and the literals afterwards would satisfy a set comparison while breaking every HTTP/3 client. It is a gate group, so it only vetoes, and it is deliberately weaker than the scored Huffman group: it asserts that header coding still works end to end, not that any particular byte was produced.
* `reg-09-key-update` — step 09's `cap-key-update`, narrowed to a single connection pair through one key update: Carried as this step's re-check of a packet-protection observation taken off the wire. The verifier takes the connection's generation-0 traffic secrets from its keylog, derives the next generation itself, and injects a 1-RTT packet sealed with the new keys and the key-phase bit set. It proves the secrets before using them by opening one of the candidate's own packets with the generation-0 set, so a key-schedule disagreement between verifier and candidate cannot read as a candidate fault. Four assertions follow: 1. the candidate decrypts the injected packet -- so it derived the same next-generation keys; 2. the header-protection key was not rotated. This is implied rather than separately asserted: the verifier applied generation-0 header protection, so a candidate that rotated it could not find the packet number and nothing after this point would succeed; 3. a second update before the first is acknowledged is `Err(Error::KeyUpdate)`; 4. the candidate's own next short-header packet has the phase bit set. Carried because it is the one inherited group here that reaches the wire. The other two end at the HTTP/3 API, and a candidate can satisfy both while producing short-header packets the peer cannot open. This step rewrites the lost-frame handler, which decides what goes into a retransmitted packet and therefore runs inside the same send path that seals every 1-RTT packet; a key schedule or key-phase bookkeeping disturbed there breaks this group and nothing else in the file. It is also the cheapest orthogonality available: everything else inherited is header coding, because header coding is what moved. It reads no clock and waits for no timer. Everything it observes is a property of bytes that were produced.
* `reg-01-h3-get` — step 01's `cap-h3-get`, re-run unchanged: The end-to-end HTTP/3 fetch, run between the candidate's own `quiche-server` and its own `quiche-client`, as two real processes over UDP loopback. The server is started on a loopback port serving a 64 KiB file the verifier wrote. The client fetches it with `--no-verify` and with `--http-version HTTP/3` pinned rather than left to the apps' own negotiation, exits 0 within the group's deadline, and the bytes on its stdout match the file byte-for-byte. Pinning the version is what keeps the inherited observation HTTP/3 and stops it quietly becoming a different group. This is the plan-mandated end-to-end regression, and it is carried at every step for the same reason: it is the one observation that requires the whole stack -- the workspace, the transport crate, the HTTP/3 layer, QPACK, the examples and the two binaries -- to be simultaneously correct. This step adds two workspace members and moves a piece of QPACK's machinery into a different crate, either of which can produce a package that compiles and does not work. Ports are chosen free per case and the driver runs one case per `cargo test` invocation, so there is nothing to contend over; the harness's `serial_t1` guard is defined but called by no case at this step, and the serialisation the sentence above describes is the driver's, not a lock the case takes.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

