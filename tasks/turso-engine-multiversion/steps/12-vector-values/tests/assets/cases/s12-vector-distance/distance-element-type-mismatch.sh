
# R12.10: a vector_distance_l2 call mixing a float32 and a float64 vector of the
# SAME dimension is refused rather than coerced to a common width. What is
# observed is the refusal, and that the same-width calls on either side of it
# still answer.
# The two element widths are distinct types in the layout, and mixing them in
# one distance call is refused with its own diagnostic rather than being
# coerced silently. Same-type calls on either side still work.
#
# The wording of a diagnostic is not graded. Every line carrying the
# shell's box-drawing error marker is folded to the single word ERROR, so
# what is asserted is that the refusal lands at exactly that point in the
# stream, that it produced no rows of its own, and that what comes after it
# is unharmed. Blank separator lines come and go along the release chain and
# are removed; every row printed below has two or more columns, so nothing
# removed could have been a result row.
#
# No stock sqlite3 cross-read, and none is possible: vector_distance_l2 and the
# vector constructors are engine surface with no counterpart in stock sqlite3
# (spec/PROFILE.md rule 9), which refuses the call with "no such function", so
# there is no second opinion to compare this against. What stands in for one is
# arithmetic: the two same-width calls flanking the refusal are 3-4-5
# triangles, whose distance is 5 whatever this engine does.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
SELECT vector_distance_l2(vector32('[3,4]'), vector64('[0,0]'));
SELECT vector_distance_l2(vector32('[3,4]'), vector32('[0,0]'));
SELECT vector_distance_l2(vector64('[3,4]'), vector64('[0,0]'));
SELECT 'still running';
SQL
