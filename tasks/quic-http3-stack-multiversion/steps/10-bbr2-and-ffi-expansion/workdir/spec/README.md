# Specification material — step 10

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9000.txt` | 18.2 (transport parameter definitions), 3.5 (solicited state transitions), 19.4 (RESET_STREAM frames), 19.5 (STOP_SENDING frames), 8.2 (path validation), 8.2.2 (path validation responses), 19.17 (PATH_CHALLENGE frames), 5.1.1 (issuing connection IDs), 5.1.2 (consuming and retiring connection IDs), 9 (connection migration) | 18.2 is the authority for `TransportParams`: which parameters exist, which have defaults and what those defaults are, which is what makes a parameter the peer never sent readable as a number rather than as an absence. 3.5 with 19.4 and 19.5 is the reason the four new stream counters are asymmetric between the two endpoints -- a STOP_SENDING frame *requests* a RESET_STREAM, so one shutdown produces a stopped count on one side and a reset count on the other. 8.2.2 is the obligation `set_path_challenge_recv_max_queue_len` bounds: an endpoint MUST echo every PATH_CHALLENGE it receives, and a queue limit is how an implementation refuses to be made to do unbounded work by a peer that challenges faster than it can answer. 19.17 is the frame `path_challenge_rx_count` counts. 5.1.1 and 5.1.2 are the behaviour behind the four renamed methods, and the reason the rename is only a rename: they draw the line between the connection IDs an endpoint issues (its *source* IDs, now `scid`) and the ones it uses to address its peer (its *destination* IDs, now `dcid`), and that line is what the new names make explicit. Section 9 is what the newly exposed C entry points drive. |
| `rfc9002.txt` | 7 (congestion control), 7.2 (initial and minimum congestion window), 7.3 (congestion control states), 4.7 (probe timeout) | Background for the fourth congestion-control algorithm. RFC 9002 specifies the Reno-shaped default, not BBRv2, and that is the point: 7.3's slow-start / recovery / congestion-avoidance state machine is the behaviour a BBR-family controller deliberately does *not* implement, so the only things a group can fairly assert of BBRv2 are the ones 7 and 7.2 make universal -- the window starts at the initial window, grows while the path is not congested, and every byte handed to the transport is eventually sent. |
| `rfc9114.txt` | 7.2 (frame definitions), 7.2.6 (GOAWAY), 4.1 (HTTP message framing) | The HTTP/3 events the C API reports, and the numbering that had to be corrected: the six `quiche_h3_event_type` values are the observable outcomes of this section's frames. Read for GOAWAY in particular, which is the first of the three values whose returned integer moves at this milestone. |
| `rfc9221.txt` | 3 (datagram frame types), 4 (behavior and usage) | Unchanged at this milestone and cited so it stays that way. The transport DATAGRAM frame API is what the inherited datagram group exercises, and the two new C predicates over its queues are mirrors of step 04's Rust ones. Nothing about the frame or its flow-control exemption moves here. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

