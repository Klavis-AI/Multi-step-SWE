# R11.8 and R11.3: a query that matches no rows contributes no bytes to
# stdout -- no header, no placeholder, and no separator line either. The
# marker query that follows it has to land on the very next line.
#
# Stock sqlite3 rendering the same script is the second opinion R11.3
# promises; every statement here is ordinary SQL it answers too. Its standard
# error is folded into its output on purpose, so a stock failure fails the
# comparison rather than passing silently.
set -u
cat > script.sql <<'SQL'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2);
SELECT a FROM t WHERE a > 99;
SELECT 'marker';
SELECT a FROM t WHERE 0;
SELECT count(*) FROM t;
SQL
mkdir engine stock
( cd engine && "$CLI" -m "$CLI_MODE" -q main.db < ../script.sql ) > engine.out 2>/dev/null
( cd stock && "$SQLITE3" -batch -list -separator '|' -noheader main.db < ../script.sql ) > stock.out 2>&1
cat engine.out
if cmp -s engine.out stock.out; then echo "same_as_stock yes"; else echo "same_as_stock no"; fi
