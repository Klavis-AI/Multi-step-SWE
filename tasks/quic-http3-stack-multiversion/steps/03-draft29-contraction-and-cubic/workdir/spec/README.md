# Specification material — step 03

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9001.txt` | 5.2, 5.8 | Initial Secrets (Section 5.2) and Retry Packet Integrity (Section 5.8): the same HKDF-Extract-with-salt and AEAD-pseudo-packet methodology this milestone reapplies with draft-29's own salt and Retry key/nonce. Those two constants are draft-29-specific and have no RFC of their own (only the final v1 values in this section are normative here), so both are given inline in the groups below instead. |
| `rfc9218.txt` | 4, 4.1, 4.2 | Priority Parameters, Urgency and Incremental: the semantic model Connection::stream_priority() implements locally at this milestone. The wire-level Priority header field (Section 5) and PRIORITY_UPDATE frame (Section 7) this RFC also defines are not part of this release; see "required to exist, not scored at this step" below. |
| `rfc9002.txt` | Appendix B | The loss-detection and congestion-control pseudocode, and specifically the order it fixes for one acknowledgement: `DetectAndRemoveLostPackets`/`OnPacketsLost` before `OnPacketsAcked` (B.4), and no window growth while `InCongestionRecovery(acked_packet.time_sent)` holds (B.5). CUBIC's own arithmetic is not in this RFC, but the event path the congestion-control groups measure `cwnd` through is, and a module that acks first and detects loss afterwards reports a different window at the moment it is read. |
| `rfc9001-appendix-a.txt` | Appendix A | The worked TLS 1.3 Initial-key example appendix-a-vectors.json decomposes into named byte strings; the grader replays the same file, so a disagreement with it is a disagreement with this appendix. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

