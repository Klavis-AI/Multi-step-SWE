# Workspace handoff — step 12

You are continuing the workspace step 11 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

The previous step left path MTU discovery, a recovery module rebuilt into a
`congestion` submodule with a round-trip estimator of its own, a configurable
anti-amplification factor, public wire error codes for both layers, and an
`acked_bytes` counter on `Stats`. All of it stays, and this step takes **nothing** away from the public
surface -- it only adds beside it.

One change inside the crate is worth naming so that it does not read as a
removal when you meet it: the `ring`-backed `HeaderProtectionKey` that lived in
`crypto/mod.rs` is gone, replaced by per-backend implementations in
`crypto/boringssl.rs` and `crypto/openssl_quictls.rs`. That type was never
public -- `crypto` is declared `mod crypto;`, not `pub mod`, before and after
this release -- so nothing outside the crate could name it, nothing in this
package references it, and no group at any step looks for it. It is
reimplemented, not deleted, and it is mentioned here only because a reader
diffing the two trees will see the old file lose a type and should not go
hunting for a public break that does not exist.

The workspace gains a member. Whatever builds the workspace must build `h3i`
too, and the binary it produces is named for the crate.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-11-pmtud` — step 11's `cap-pmtud`, narrowed to the server side of the probe: Carried as the previous milestone's headline. Both endpoints are configured with `set_max_send_udp_payload_size(1452)` and `set_max_recv_udp_payload_size(1452)`, and the only thing that differs between the two legs is `discover_pmtu`: * **on** -- at least one **server** datagram is exactly 1452 bytes, and every server datagram is either at or under 1200 or exactly 1452, with nothing in between; * **off** -- no server datagram exceeds 1200 at all; * **both** -- a 64 KiB body arrives byte-for-byte with the stream finished, and neither end of the on-configuration pair is closed afterwards. Carried because this step adds a workspace member that opens its own connections and a setter that re-encodes a live connection's transport parameters, and because PMTU discovery is the youngest behaviour in the stack -- a rebuild of the send path would show here first. **Three of step 11's legs are deliberately left behind**, and each for the same reason: a gate case is an unpriced veto, so anything in it that could read differently under load costs every candidate the whole step. * The **probe count and position** were never asserted at step 11 either, and are not asserted here. * The **client histograms** -- `{43:1, 483:1, 904:1, 936:1, 1050:1, 1200:54}` with discovery on against `{43:1, 483:1, 932:1, 962:1, 1452:45}` with it off -- are dropped. They are a statement about how a 64 KiB write is chopped up, which depends on the congestion window at each `send`, and the exact multiset is not something a gate should be asserting. The *bound* survives in the server legs above, where it is one-sided. * The **binary-search-on-loss** leg was already absent at step 11, for the reason recorded there: the reference needed more timer ticks than a bounded case can justify before it re-probed at a smaller size. `PathStats::pmtu` is not read, at step 11 or here: it reports the *configured* maximum from the moment the connection is created and reads 1452 in both legs before either has sent a packet, so it cannot tell them apart.
* `reg-09-key-update` — step 09's `cap-key-update`, re-seeded from step 10's copy of it and unchanged: The verifier becomes the peer that initiates the update, because there is no public API for initiating one and the candidate is only required to implement the responder: from `CLIENT_TRAFFIC_SECRET_0` in the candidate's own keylog it derives the next generation itself -- HKDF-Expand-Label with `quic ku`, then `quic key` and `quic iv` -- and builds a protected 1-RTT packet with the key- phase bit set. It proves those keys first by opening one of the candidate's own packets with them, so that a key-schedule disagreement cannot read as a candidate fault. Then: the candidate decrypts the injected STREAM frame and `stream_recv` returns the payload byte-for-byte; its own next packet has the phase bit set; and a second update before the first is acknowledged is `Err(Error::KeyUpdate)`. Carried because this step's new setter re-encodes a live connection's transport parameters and its new workspace member drives connections of its own, and every short-header packet either of those produces is sealed by exactly this machinery. The header-protection reuse leg is implied rather than separately asserted, as at step 09: the verifier applies header protection with the generation-0 key, so a candidate that rotated it could not find the packet number and nothing later would succeed.
* `reg-01-h3-get` — step 01's `cap-h3-get`, re-run unchanged: Inherited from step 01, re-seeded from step 07, the most recent step to carry it: a 64 KiB body travels from the candidate's own `quiche-server` to the candidate's own `quiche-client` over UDP loopback, byte for byte, as two real processes, with `--http-version HTTP/3` pinned so the observation stays HTTP/3 rather than whatever the apps would negotiate. Carried because this step reaches into the HTTP/3 layer twice -- a new way to send header sections and a new statistics surface -- and because it is the only inherited gate case here that exercises the whole stack as the user gets it. `cap-h3i-replay` also crosses the process boundary, but through a new binary and against a hand-written action file; this one is the ordinary path, and if it fails, that one's diagnosis is guesswork.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

