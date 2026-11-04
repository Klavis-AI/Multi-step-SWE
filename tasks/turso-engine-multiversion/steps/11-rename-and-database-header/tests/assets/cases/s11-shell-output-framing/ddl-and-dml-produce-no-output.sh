# R11.8 and R11.3: statements that return no rows contribute no bytes at all.
# A script that is all DDL and DML followed by one SELECT must print exactly
# that SELECT's rows, with nothing before them -- no acknowledgement, no
# affected-row count, no separator line.
#
# Stock sqlite3 running the same script in the same rendering mode is the
# second opinion R11.3 promises, and it is a real one here: every statement in
# the script is ordinary SQL that stock answers too, so a mismatch is a
# finding and not an expected divergence. Its standard error is folded into
# its output on purpose, so a stock failure fails the comparison loudly.
set -u
cat > script.sql <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'one');
INSERT INTO t VALUES(2,'two');
UPDATE t SET b = 'TWO' WHERE a = 2;
DELETE FROM t WHERE a = 1;
CREATE TABLE u(x);
DROP TABLE u;
SELECT a, b FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
mkdir engine stock
( cd engine && "$CLI" -m "$CLI_MODE" -q main.db < ../script.sql ) > engine.out 2>/dev/null
( cd stock && "$SQLITE3" -batch -list -separator '|' -noheader main.db < ../script.sql ) > stock.out 2>&1
cat engine.out
if cmp -s engine.out stock.out; then echo "same_as_stock yes"; else echo "same_as_stock no"; fi
