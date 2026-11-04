# Specification material — step 05

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9000.txt` | 15 (version numbers), 17.2 (long header packets), 17.2.5 (Retry), 19 (frame types) | Version 1 is `0x00000001` and this is the document that says so. The long header layout, the Retry packet and the frame encodings are the ones the drafts already used; what changes is which version number is authoritative and that an endpoint now has to hold more than one of them at once. |
| `rfc9001.txt` | 5.2 (initial secrets), 5.4 (header protection), 5.8 (Retry packet integrity), Appendix A (sample packet protection) | Section 5.2 carries version 1's Initial salt, `38762cf7f55934b34d179ae6a4c80cadccbb7f0a`, and Section 5.8 carries version 1's Retry integrity key and nonce. Appendix A's worked example is a complete version-1 client Initial with its keys and its ciphertext, and it is the reference the grader's own implementation is checked against; a stack that disagrees with the appendix disagrees with the endpoint on the other side of every T2 group at this step. |
| `rfc9002.txt` | 7.7 (pacing) | `SendInfo.at` is the pacer's output. Section 7.7 describes the interval a sender should leave between packets of a congestion window and why a sender that ignores it bursts; this release moves that decision out of the send call and into a value the caller is expected to honour. |
| `rfc9114.txt` | 3.1 (discovering an HTTP/3 endpoint), 7 (frames) | The `h3` ALPN token this milestone adds is the one RFC 9114 registers for HTTP/3 over QUIC version 1. The draft tokens beside it are the same protocol with an earlier transport underneath. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

