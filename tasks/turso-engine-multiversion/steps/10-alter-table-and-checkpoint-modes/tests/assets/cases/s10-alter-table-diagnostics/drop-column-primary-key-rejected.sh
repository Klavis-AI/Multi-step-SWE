# R10.6: DROP COLUMN on the table's INTEGER PRIMARY KEY is refused, produces
# no rows, and leaves the schema and the rows as they were -- the statements
# after it still see the original shape.
#
# DROP COLUMN is not allowed to remove a column the table's primary key
# depends on. The engine must report an error and leave the schema and rows
# unchanged, so the statement after it still sees the original shape.
#
# The wording is not graded: spec/TEST_INTERFACE.md's "error report" fold
# collapses the refusal to a single ERROR line. Stock sqlite3 refuses the same
# statement (`cannot drop PRIMARY KEY column: "id"`) and leaves the same
# shape behind, which the last lines check without pinning its wording either.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
CREATE TABLE t(id INTEGER PRIMARY KEY, payload TEXT);
INSERT INTO t VALUES(1,'a'),(2,'b');
ALTER TABLE t DROP COLUMN id;
SELECT cid, name FROM pragma_table_info('t') ORDER BY cid;
SELECT id, payload FROM t ORDER BY id;
SELECT count(*) FROM t;
SQL
# Stock sqlite3 is the second opinion on the refusal: a .sh case is outside
# the generator's automatic comparison, so the case makes it itself. Only
# accept-or-refuse and the resulting shape are compared, never the wording.
if "$SQLITE3" -batch stock.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE t(id INTEGER PRIMARY KEY, payload TEXT);
INSERT INTO t VALUES(1,'a'),(2,'b');
ALTER TABLE t DROP COLUMN id;
SQL
then echo "stock_sqlite3: accepted"
else echo "stock_sqlite3: refused"
fi
echo "stock_shape:"
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL'
SELECT cid, name FROM pragma_table_info('t') ORDER BY cid;
SELECT id, payload FROM t ORDER BY id;
SQL
