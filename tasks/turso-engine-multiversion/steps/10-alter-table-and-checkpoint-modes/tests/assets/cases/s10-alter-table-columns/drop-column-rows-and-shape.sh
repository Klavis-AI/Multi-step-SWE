# R10.5: DROP COLUMN removes the field from every row's record, not only from
# the schema -- `SELECT *` returns the narrowed rows and a row inserted
# afterwards lines up with them.
#
# DROP COLUMN has to remove the field from every row's record, not only from
# the schema: a `SELECT *` after the drop must return the narrowed rows, and
# a row inserted afterwards must line up with them.
#
# Being a .sh case it is outside the generator's automatic stock-sqlite3
# comparison, so the last lines make that comparison themselves: every value
# here is one stock sqlite3 produces for the same statements.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE t(a INTEGER, b TEXT, c INTEGER);
INSERT INTO t VALUES(1,'x',10),(2,'y',20);
ALTER TABLE t DROP COLUMN b;
INSERT INTO t VALUES(3,30);
SELECT * FROM t ORDER BY a;
SELECT cid, name, type FROM pragma_table_info('t') ORDER BY cid;
SELECT sum(c) FROM t;
SELECT count(*) FROM t;
SQL
# Stock sqlite3's stderr is merged on purpose: a stock-side failure must land
# in the graded output rather than vanish.
echo "stock_sqlite3:"
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT, c INTEGER);
INSERT INTO t VALUES(1,'x',10),(2,'y',20);
ALTER TABLE t DROP COLUMN b;
INSERT INTO t VALUES(3,30);
SELECT * FROM t ORDER BY a;
SELECT cid, name, type FROM pragma_table_info('t') ORDER BY cid;
SELECT sum(c) FROM t;
SELECT count(*) FROM t;
SQL
