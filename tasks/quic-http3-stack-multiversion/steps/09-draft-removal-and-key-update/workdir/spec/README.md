# Specification material — step 09

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9001.txt` | 5.1 (packet protection keys), 5.4 (header protection), 6 (key update), 6.1 (initiating a key update), 6.2 (responding to a key update), 6.3 (timing of receive key generation) | Section 6 is this step's headline capability, and the three sub-sections are where its rules actually live: that the key-phase bit selects the generation, that the header-protection key is **not** updated (5.4 and 6 together), and that a peer may not update again until its first update is acknowledged. Section 5.1's `"quic ku"` label is the derivation the responder has to reproduce exactly. |
| `rfc9000.txt` | 6 (version negotiation), 6.1 (sending version negotiation packets), 17.2.1 (version negotiation packet), 5.1.1 (issuing connection IDs) | The drafts are gone, so version negotiation stops being a corner and becomes the path every non-version-1 client takes. 17.2.1 is the packet layout and 6.1 is the connection-ID echo rule, both of which the removal group asserts byte-wise. 5.1.1 is the background for `source_ids` and `retired_scids`. |
| `rfc9002.txt` | 7.2 (initial and minimum congestion window) | The recommended initial window, and the reason the new setter counts packets rather than bytes: the recommendation is itself expressed in packets. |
| `rfc9218.txt` | 4 (priority parameters), 4.1 (urgency), 4.2 (incremental) | The two priority parameters `stream_readable_next` and `stream_writable_next` order by, and in particular what "incremental" is supposed to mean: an incremental stream is one a server may serve in chunks alongside its peers, which is why equal-urgency incremental streams round-robin rather than finishing in ID order. |
| `rfc9297.txt` | 2 (HTTP datagrams), 2.1 (HTTP/3 datagrams) | Read to understand what is being withdrawn. The HTTP/3 datagram API deleted here was an implementation of an earlier draft of this document; the transport-level DATAGRAM frame it sat on top of (RFC 9221) is untouched. The distinction is exactly the one this step's removal group and its inherited datagram group are drawn around. |

Two more of the shipped texts are **context rather than requirements** at this
step: `rfc9369.txt` (QUIC version 2) and `rfc9368.txt` (compatible version
negotiation). Neither is load-bearing here -- version 2 does not exist yet, and
the negotiation this step scores is RFC 9000 Section 6.1's, not the compatible
scheme -- but the corpus ships whole rather than per step, and the question
"does this milestone need them?" is answered above rather than by their
presence in the directory.

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

