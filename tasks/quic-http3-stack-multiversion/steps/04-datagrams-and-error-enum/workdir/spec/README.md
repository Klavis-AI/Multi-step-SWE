# Specification material — step 04

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9221.txt` | 3 (transport parameter), 4 (datagram frame types), 5 (behavior and usage, including 5.2 acknowledgement handling and 5.3 flow control) | The DATAGRAM extension as implemented at quiche 0.7.0. quiche 0.7.0 (2021-02-01) predates this RFC's publication (2022-03) and actually tracks draft-ietf-quic-datagram-01, but that draft is not in this task's seeded spec corpus and its wire format is unchanged in the RFC: the frame types are 0x30 and 0x31 (RFC 9221 §4), the transport parameter is `max_datagram_frame_size` (0x20, RFC 9221 §3), and the semantics are unreliable delivery with bounded queuing (§5). Treat this corpus copy as the normative text for those facts. |
| `rfc9000.txt` | 13.3 (retransmission) | DATAGRAM frames are never retransmitted and do not contribute to loss recovery. They are ack-eliciting but not retransmittable, a new category in the loss state machine. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

