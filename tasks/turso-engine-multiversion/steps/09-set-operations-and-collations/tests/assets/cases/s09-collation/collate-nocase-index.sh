# R09.6: an index that carries COLLATE NOCASE serves a case-insensitive
# equality and a range scan, and returns exactly the rows the same query
# returns from a table that carries the same collation with no index -- and
# the rows stock sqlite3 returns for the same script.
#
# Before v0.0.21 the collation is not applied, so the equalities miss and the
# ordering falls back to BINARY. The last line is the differential: a .sh case
# is not covered by the generator's automatic sqlite3 comparison, so the case
# makes that comparison itself.
#
# DELIBERATELY NOT group_concat, for the same reason milestone 9's gate case
# is not. `lang_aggfunc.html` -- which instruction.md points the agent at --
# states that group_concat's concatenation order is arbitrary unless the
# ORDER BY is an argument of the aggregate itself; an ORDER BY in a
# FROM-clause subquery does not bind it. An engine that flattened the
# subquery and concatenated in another order would be conforming and would
# still fail this case, losing the whole group for a correct solution. Every
# ordering below is therefore asserted one row per line under a top-level
# ORDER BY, which IS defined, over a total order -- the unique `a`, or `b`
# then the unique `a`. Do not "tidy" these back into a group_concat.
#
# What this case does NOT grade, and cannot: whether the planner actually
# consulted `ix_b`. EXPLAIN and EXPLAIN QUERY PLAN are out of scope at this
# step (TEST_INTERFACE.md, "What this milestone does not include"), so there
# is no observable for plan choice. What is falsifiable here is the
# collation the index carries: if `ix_b` were keyed BINARY and used, the
# `ix-eq` and `ix-ab` lines would return one row where `noix-eq` and
# `noix-ab` return two, and the case would fail.
set -u

# Index support is opt-in from v0.0.22 to v0.1.3 and the switch changes shape
# across that span, so ask the shell what it accepts rather than assuming. An
# engine with no such option gets no option.
IDX=""
case "$("$CLI" --help 2>&1)" in
  *"--experimental-indexes <"*) IDX="--experimental-indexes=true" ;;
  *"--experimental-indexes"*)   IDX="--experimental-indexes" ;;
esac

cat > script.sql <<'SQL'
CREATE TABLE ix(a INTEGER, b TEXT COLLATE NOCASE);
CREATE TABLE noix(a INTEGER, b TEXT COLLATE NOCASE);
INSERT INTO ix VALUES(1,'B'),(2,'a'),(3,'b'),(4,'A'),(5,'c'),(6,'AB'),(7,'ab');
INSERT INTO noix VALUES(1,'B'),(2,'a'),(3,'b'),(4,'A'),(5,'c'),(6,'AB'),(7,'ab');
CREATE INDEX ix_b ON ix(b);
SELECT 'ix-eq:'||a FROM ix WHERE b='a' ORDER BY a;
SELECT 'noix-eq:'||a FROM noix WHERE b='a' ORDER BY a;
SELECT 'ix-ab:'||a FROM ix WHERE b='AB' ORDER BY a;
SELECT 'noix-ab:'||a FROM noix WHERE b='AB' ORDER BY a;
SELECT 'ix-miss:'||count(*) FROM ix WHERE b='zz';
SELECT 'noix-miss:'||count(*) FROM noix WHERE b='zz';
SELECT 'ix-range:'||a FROM ix WHERE b>'aa' ORDER BY a;
SELECT 'noix-range:'||a FROM noix WHERE b>'aa' ORDER BY a;
SELECT 'ix-order:'||b FROM ix ORDER BY b, a;
SELECT 'ix-total:'||count(*) FROM ix;
SQL

"$CLI" -m "$CLI_MODE" $IDX -q main.db < script.sql 2>/dev/null \
  | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/' > engine.txt
"$SQLITE3" -batch -list -separator '|' -noheader ref.db < script.sql 2>/dev/null \
  | sed '/^[[:space:]]*$/d' > stock.txt

cat engine.txt
if cmp -s engine.txt stock.txt; then
  echo 'sqlite3-agrees:yes'
else
  echo 'sqlite3-agrees:no'
fi
