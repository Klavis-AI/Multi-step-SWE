# Specification material — step 07

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9000.txt` | 16 (variable-length integer encoding), 17 (packet formats), 19 (frame types) | Section 16 and its Appendix A worked values are what the `octets` crate encodes and decodes; moving the module into its own package must leave the encoding bit-identical. The packet and frame layouts are the ones the reader is used on. |
| `rfc9218.txt` | 4 (priority parameters), 7 (the PRIORITY_UPDATE frame), 7.2 (its HTTP/3 form) | This is the document that defines `PRIORITY_UPDATE`, its two frame type codepoints `0xF0700` and `0xF0701`, the Prioritized Element ID field and the structured-field priority value that follows it. It is also where the rule that the most recently received update wins comes from, which is what makes the event edge-triggered rather than queued. Section 7.1 is the HTTP/2 variant and Section 8 is about merging client- and server-driven priorities; neither is implemented here. |
| `rfc9114.txt` | 6.2.1 (control streams), 7.2.4 (SETTINGS), 8.1 (HTTP/3 error codes) | `PRIORITY_UPDATE` is a control-stream frame, so the control-stream rules decide who may send it and what happens when the wrong endpoint does. Section 8.1 fixes the two error codes this step's guards produce: `H3_FRAME_UNEXPECTED` is `0x105` and `H3_ID_ERROR` is `0x108`. |
| `rfc9002.txt` | 7 (congestion control), 7.2 (initial and minimum congestion window), 7.7 (pacing) | `send_quantum()` exposes the burst budget these sections describe, and the initial congestion window of ten packets is where its starting value comes from. The congestion-control trait rework and HyStart++ draft-04 sit behind the same sections. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

