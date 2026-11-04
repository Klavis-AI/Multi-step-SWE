# Workspace handoff — step 03

You are continuing the workspace step 02 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

The previous step introduced version negotiation and pluggable congestion control 
with Reno as the sole algorithm. Four QUIC versions were supported: drafts 23, 24, 
25, and 27, and HTTP/3 offered the h3-27/h3-25/h3-24/h3-23 ALPN strings.

There was no stream-priority API of any kind: `priority` and `urgency` do not 
appear anywhere in the previous milestone's source. `Connection::stream_priority` 
is new here, so there is nothing to migrate away from and no older priority 
scheme to keep compatible with.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-01-initial-keys` — step 01's `cap-initial-keys`, re-run unchanged: The first datagram the candidate's client sends is opened by the grader's own implementation: Initial secrets from the salt for the version on the wire, header protection removed with the AES-ECB mask, packet protection removed with AEAD_AES_128_GCM, and the plaintext then contains a CRYPTO frame at offset 0 whose bytes begin a well-formed TLS 1.3 ClientHello — handshake type 1, legacy_version 0x0303, a legacy_session_id of at most 32 bytes, a non-empty even-length cipher_suites list. The version must be one this milestone still supports, and the datagram must be padded to 1200 bytes. Nothing in the candidate's library is used to read it.
* `reg-01-h3-get` — step 01's `cap-h3-get`, re-run unchanged: The candidate's http3-client fetches a 64 KiB pseudorandom file from the candidate's http3-server over UDP loopback and writes it to stdout byte for byte: control streams, SETTINGS, a QPACK-encoded HEADERS frame and DATA, end to end. A request for a path that is not in the document root is answered rather than left to hang.
* `reg-01-stream-transfer` — step 01's `cap-stream-transfer`, re-run unchanged: The candidate's client fetches a 512 KiB pseudorandom file from the candidate's server over UDP loopback and writes it to stdout byte for byte. Large enough to cross the initial congestion window, the initial flow-control window and many packet boundaries, so a stack that reassembles out-of-order or retransmitted data incorrectly produces the wrong bytes rather than the right ones by luck.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

