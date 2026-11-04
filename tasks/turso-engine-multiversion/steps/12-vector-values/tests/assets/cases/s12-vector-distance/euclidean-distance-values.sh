
# R12.4, positive half: the value vector_distance_l2 returns for pairs whose
# Euclidean distance is fixed by arithmetic alone -- a 3-4-5 triangle, a vector
# against itself, a one-dimensional pair, and a pair whose squared distances sum
# to a perfect square -- at both element widths.
# The Euclidean distance function arrives at this checkpoint; the vector
# constructors and the cosine distance already existed, so the L2 function is
# the only new name here. Each pair below has a distance known without
# reference to any implementation: a 3-4-5 triangle, a vector against itself,
# a one-dimensional pair, and a pair whose squared distances sum to a perfect
# square. Both element widths are covered, and the exact rendering of the REAL
# is part of the value.
#
# No stock sqlite3 cross-read, and none is possible: vector_distance_l2 and the
# vector constructors are engine surface with no counterpart in stock sqlite3
# (spec/PROFILE.md rule 9), which refuses the call with "no such function", so
# there is no second opinion to compare this against. What stands in for one is
# arithmetic: every pair below has a distance derivable without reference to
# any implementation, which is why the pairs were chosen.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
SELECT vector_distance_l2(vector('[0,0]'), vector('[3,4]'));
SELECT vector_distance_l2(vector('[3,4]'), vector('[0,0]'));
SELECT vector_distance_l2(vector('[1,2,3]'), vector('[1,2,3]'));
SELECT vector_distance_l2(vector('[-1]'), vector('[2]'));
SELECT vector_distance_l2(vector('[1,2,2]'), vector('[0,0,0]'));
SELECT vector_distance_l2(vector64('[0,0]'), vector64('[3,4]'));
SELECT vector_distance_l2(vector64('[6,8]'), vector64('[0,0]'));
SELECT count(*) FROM (SELECT 1);
SQL
