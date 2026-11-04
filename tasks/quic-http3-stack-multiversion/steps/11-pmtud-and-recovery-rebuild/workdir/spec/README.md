# Specification material — step 11

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9000.txt` | 14 (datagram size), 14.1 (initial datagram size), 14.2 (path maximum transmission unit), 14.3 (datagram size during and after handshake), 14.4 (sending QUIC PMTU probes), 8 (address validation), 8.1 (address validation during connection establishment), 17.1 (packet number encoding and decoding), A.2 (sample packet number encoding algorithm), 20.1 (transport error codes) | Section 14 is the whole of the first feature. 14.1 is where the 1200-byte floor comes from and why a connection with discovery enabled starts there rather than at whatever the socket would allow: an endpoint MUST NOT send an initial packet larger than the path is known to support, and 1200 is the size every path is required to support. 14.2 and 14.3 say the estimate may rise only on evidence and must fall back on loss. 14.4 is the probe itself -- padded, sent alone, and *not* counted as an ordinary loss when it fails, which is exactly why the search can afford to be aggressive. Section 8 and 8.1 are the anti-amplification limit: the three-times bound, that it is measured in bytes received rather than datagrams, and that a server subject to it sends *less* rather than refusing to send. Read together with the new setter they are why factor 1 yields a truncated real flight rather than silence. 17.1 and A.2 are the packet-number rule, and A.2 in particular is the algorithm the library now implements literally: the number of bits must be at least one more than the base-2 logarithm of the number of packets that could still be in flight. Read 17.1 for why the old magnitude-based rule was not merely inefficient but wrong -- it is the *unacknowledged range*, not the absolute value, that determines whether the receiver can reconstruct the number. 20.1 is the table `quiche::WireErrorCode` now names, value for value. |
| `rfc9114.txt` | 8.1 (HTTP/3 error codes), 6.2.1 (control streams), 7.2.1 (DATA frames), 7.2.4 (SETTINGS frames), 7.2.4.1 (defined SETTINGS parameters), 7.2.4.2 (initialization) | 8.1 is the table `h3::WireErrorCode` names. 6.2.1 with 7.2.1 is the violation that binds one entry of that table to real behaviour: DATA frames are not permitted on a control stream, and an endpoint that receives one MUST respond with H3_FRAME_UNEXPECTED -- 0x105, the number the new enum spells. 7.2.4 and 7.2.4.1 are the reason `set_additional_settings` refuses the identifiers it refuses and accepts everything else: an endpoint MUST ignore settings it does not understand, which is what makes sending unknown identifiers safe, while sending a *known* identifier twice in one frame is a connection error. 7.2.4.2 is why the receiving side can report them all faithfully in order. |
| `rfc9002.txt` | 2 (conventions and definitions), 5 (estimating the round-trip time), 6.2 (probe timeout), 7 (congestion control) | Background for the module that was rebuilt, and the reason the rebuild is scored as layout rather than behaviour. Section 5's round-trip estimator is now a component of its own in `recovery/rtt.rs`, and Section 7's controllers are now a `congestion` submodule behind one trait -- but RFC 9002 specifies none of that structure, and it specifies no observable that distinguishes the new arrangement from the old. What it does supply is the one behavioural trace the rebuild leaves: Section 2's definition of an acknowledged packet is what the new `Stats::acked_bytes` counts. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

