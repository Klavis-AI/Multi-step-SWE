# Specification material — step 08

The full text of every RFC this task touches is in this directory,
byte-identical to the IETF originals (`SHA256SUMS` covers them). It is
here so the milestone can be worked offline and so that every step reads
the same words. This environment has no general network access, so these
documents and what the image already holds are all the reference material
there is.

What is load-bearing *for this step*:

| document | sections | why it matters here |
| --- | --- | --- |
| `rfc9000.txt` | 9 (connection migration), 9.3 (responding to connection migration), 8.2 (path validation), 5.1 (connection ID) | Section 9 is the normative account of what a migration is allowed to look like, and 8.2 is the PATH_CHALLENGE/PATH_RESPONSE exchange that `probe_path` starts and `PathEvent::Validated` reports the end of. 5.1 is why connection-ID rotation belongs in the same step as migration rather than a later one: migrating without it defeats the point, and in quiche it is also what makes probing mechanically possible. |

`appendix-a-vectors.json` holds RFC 9001 Appendix A's worked
example decomposed into named byte strings. The grader uses the same
file, so a disagreement with it is a disagreement with the RFC.

