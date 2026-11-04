# R10.6: dropping a column that does not exist is refused, produces no rows,
# and leaves the table untouched while the session carries on.
#
# Dropping a column that does not exist is a no-such-column error. The table
# is untouched and the session carries on.
#
# The wording is not graded: spec/TEST_INTERFACE.md's "error report" fold
# collapses the refusal to a single ERROR line. Stock sqlite3 refuses the same
# statement and leaves the same shape behind, which the last lines check
# without pinning its wording either.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'x');
ALTER TABLE t DROP COLUMN zz;
SELECT cid, name FROM pragma_table_info('t') ORDER BY cid;
SELECT a, b FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
# Stock sqlite3 is the second opinion on the refusal: a .sh case is outside
# the generator's automatic comparison, so the case makes it itself. Only
# accept-or-refuse and the resulting shape are compared, never the wording.
if "$SQLITE3" -batch stock.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'x');
ALTER TABLE t DROP COLUMN zz;
SQL
then echo "stock_sqlite3: accepted"
else echo "stock_sqlite3: refused"
fi
echo "stock_shape:"
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL'
SELECT cid, name FROM pragma_table_info('t') ORDER BY cid;
SELECT a, b FROM t ORDER BY a;
SQL
