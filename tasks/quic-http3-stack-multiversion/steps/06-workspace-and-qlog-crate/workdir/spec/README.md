# Specification material — step 06

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9000.txt` | 4 (flow control), 4.1 (data flow control), 4.2 (increasing flow control limits), 13.4 (explicit congestion notification), 19.3 (ACK frames), 19.3.2 (ECN counts), 19.4 (RESET_STREAM frames) | Section 19.3.2 is the layout of the three ECN-count varints and the rule that frame type `0x03` is the only ACK form that carries them; consuming exactly three varints in exactly that order is what this step adds to the frame parser. Sections 4.1 and 4.2 are the flow-control limits the autotuning receive window moves, and in particular the advice in 4.2 that a receiver should extend a limit before the sender is blocked — the window doubling is an implementation of that advice, not a departure from it. Section 19.4 is the `RESET_STREAM` frame whose error code now reaches the application. |
| `rfc9002.txt` | A.7 (on receiving an acknowledgment), 7 (congestion control) | The ECN counts on an ACK frame are input to loss recovery, and this is the document that says what a sender does with an acknowledgement it has just parsed. It also fixes the round-trip-time estimate the window autotuner compares its own update interval against. |
| `rfc9114.txt` | 7.2.4 (SETTINGS), 7.2.4.1 (defined SETTINGS parameters), 7.2.4.2 (initialisation), 8.1 (HTTP/3 error codes), 4.1 (HTTP message framing) | `SETTINGS_MAX_FIELD_SECTION_SIZE` is the name Section 7.2.4.1 gives identifier `0x6`, which is why the Rust setter is renamed here. Section 7.2.4 is also the source of the two rules `peer_settings_raw` has to be consistent with: unrecognised identifiers must be ignored rather than rejected, and the identifiers reserved for HTTP/2 compatibility must be treated as a connection error. Section 8.1 carries `H3_EXCESSIVE_LOAD` = `0x107`. |
| `rfc9218.txt` | 2 (priority parameters) | Not this step's business, and included only to say so: HTTP/3 priority stays where step 03 left it. `send_response_with_priority` and `stream_priority` are unchanged, and nothing in this milestone reads a `priority` request header. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

