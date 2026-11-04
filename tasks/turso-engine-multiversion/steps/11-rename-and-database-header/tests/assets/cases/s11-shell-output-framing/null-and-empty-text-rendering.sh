# R11.9 and R11.3: in list mode a NULL and a zero-length text value both
# render as nothing at all, so a single-column row holding either is an empty
# line, and only typeof() tells the two apart. That is exactly the rendering a
# .sql case cannot assert -- its blank lines are stripped before comparison --
# so it is asserted here, where stdout is taken verbatim.
#
# Stock sqlite3 rendering the same script is the second opinion R11.3
# promises, and spec/PROFILE.md rule 4's empty rendering of NULL is stock's
# own `-list` rendering, so the two must agree. Its standard error is folded
# into its output on purpose, so a stock failure fails the comparison rather
# than passing silently.
set -u
cat > script.sql <<'SQL'
CREATE TABLE t(id INTEGER PRIMARY KEY, v);
INSERT INTO t VALUES(1,NULL),(2,''),(3,'x'),(4,0);
SELECT v FROM t ORDER BY id;
SELECT id, v, typeof(v) FROM t ORDER BY id;
SELECT count(*) FROM t;
SQL
mkdir engine stock
( cd engine && "$CLI" -m "$CLI_MODE" -q main.db < ../script.sql ) > engine.out 2>/dev/null
( cd stock && "$SQLITE3" -batch -list -separator '|' -noheader main.db < ../script.sql ) > stock.out 2>&1
cat engine.out
if cmp -s engine.out stock.out; then echo "same_as_stock yes"; else echo "same_as_stock no"; fi
