# Specification material — step 02

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9000.txt` | 6, 17.2.1 | Version Negotiation: when a server must send one, and the packet's exact layout — the zero version field, the echoed connection IDs, and the supported-version list |
| `rfc9001.txt` | 5.8 | Retry packet integrity: the AEAD_AES_128_GCM tag computed over the Retry pseudo-packet, which is what replaces draft-23's inline ODCID field. The *method* is the one this milestone uses; the key and nonce printed in this section are QUIC v1's and are not -- see the interface section for the draft-25/27 constants. |
| `rfc9114.txt` | 7.2.8, 9 | Section 9 states the general rule this milestone relies on: implementations MUST ignore unknown or unsupported values in extensible protocol elements, which is what makes the deleted DUPLICATE_PUSH type a skip rather than a rejection. Section 7.2.8 is the adjacent, narrower mechanism — the reserved frame-type ladder used to exercise that same rule — and itself points back to Section 9; it does not state the general rule on its own. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

