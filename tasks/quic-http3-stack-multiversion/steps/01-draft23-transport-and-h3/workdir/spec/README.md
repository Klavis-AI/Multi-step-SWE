# Specification material — step 01

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9000.txt` | 12 (packets and frames), 13 (delivery), 16–17 (varints, packet formats), 18–19 (transport parameters, frame types) | The framing, the variable-length integer encoding, the stream state machine and the transport parameters are the same in draft-23 as in the published RFC. The packet header formats are the same except for the Retry layout and the version number. |
| `rfc9001.txt` | 5 (packet protection), 5.1–5.3 (key derivation, AEAD), 5.4 (header protection), 8.2 (transport parameters extension) | The construction is draft-23's construction; only the Initial salt differs. Appendix A's worked example ships beside the RFC as `rfc9001-appendix-a.txt` and `appendix-a-vectors.json`, and it is the QUIC **version 1** example — salt `38762cf7…`, version `00000001` — so use it to check your HKDF and AEAD plumbing, never as this step's salt; neither file is listed in `SHA256SUMS`, which covers the ten RFC texts. Section 8.2's extension codepoint is `0xffa5` here rather than `0x39`, and the pinned BoringSSL supplies it. |
| `rfc9002.txt` | 6 (loss detection), 7 (congestion control) | Acknowledgement handling, the PTO timer and a NewReno-shaped congestion controller. Slow start's behaviour is observed at this step. |
| `rfc9114.txt` | 4 (expressing HTTP), 6 (streams), 7 (frames), 8 (error handling) | HTTP/3 over the transport you just built: the control stream, SETTINGS, the HEADERS and DATA frames, and the request/response exchange the grader drives end to end. |
| `rfc9204.txt` | 3 (static table), 4.5 (field line representations), Appendix A (the static table itself) | QPACK. At this step only the static table and literal representations are exercised; the dynamic table may be present but nothing requires it. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

