
# R12.4, negative half: a vector_distance_l2 call between vectors of different
# dimensions is refused rather than padded, truncated or answered. What is
# observed is that the refusal lands at that point in the stream, produced no
# row of its own, and left the rest of the batch running.
# Two vectors of different widths have no Euclidean distance, and the engine
# must say so rather than pad, truncate or return a number. The session is
# still usable afterwards, and the mismatch is raised the same way for vectors
# that were read out of a table as for vectors built inline.
#
# A .sh case because the diagnostic is followed by a blank line on stdout,
# which a .sql case may not carry, so the case owns its own stdout and folds
# it itself.
#
# The wording of a diagnostic is not graded. Every line carrying the
# shell's box-drawing error marker is folded to the single word ERROR, so
# what is asserted is that the refusal lands at exactly that point in the
# stream, that it produced no rows of its own, and that what comes after it
# is unharmed. Blank separator lines come and go along the release chain and
# are removed; every row printed below has two or more columns, so nothing
# removed could have been a result row.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
CREATE TABLE v(id INTEGER PRIMARY KEY, e BLOB);
INSERT INTO v VALUES(1, vector('[1,2]')),(2, vector('[1,2,3]'));
SELECT vector_distance_l2(vector('[1,2]'), vector('[1,2,3]'));
SELECT id, vector_distance_l2(e, vector('[0,0,0]')) FROM v ORDER BY id;
SELECT vector_distance_l2(vector('[1,2]'), vector('[1,2]'));
SELECT count(*) FROM v;
SQL
