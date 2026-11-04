# R10.7: PRAGMA wal_checkpoint(PASSIVE) is accepted and answers with a
# well-formed busy|frames|copied triple; the rows read back unchanged after it.
#
# PRAGMA wal_checkpoint takes a mode argument at this checkpoint, where before
# it accepted only the bare form. The parenthesised form must return SQLite's
# busy/frames/copied triple, and the rows must read back identically after the
# frames have been moved into the main database file.
#
# The triple is graded by shape, not by value: busy must be 0, the frame count
# must be positive, and the copied count must equal the frame count. The exact
# frame count depends on how many pages the engine wrote for a two-page
# database, which the contract lists as not scored, so it is not pinned here.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d' | awk -F'|' '
/^[0-9]+\|[0-9]+\|[0-9]+$/ {
    if ($1 == 0 && $2 > 0 && $2 == $3) print "checkpoint triple well-formed"
    else                                print "checkpoint triple malformed: " $0
    next
}
{ print }'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'one'),(2,'two'),(3,'three');
SELECT a, b FROM t ORDER BY a;
PRAGMA wal_checkpoint(PASSIVE);
SELECT a, b FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
# Stock sqlite3 answers the same statements on its own file. A .sh case is
# outside the generator's automatic comparison, so the case makes it itself,
# and it compares the triple the same way it grades the engine's: by shape,
# not by value. Its stderr is merged on purpose, so a stock-side failure
# lands in the graded output rather than vanishing.
echo "stock_sqlite3:"
"$SQLITE3" -batch stock.db 'PRAGMA journal_mode=WAL;' > /dev/null 2>&1
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL' | awk -F'|' '
/^[0-9]+\|[0-9]+\|[0-9]+$/ {
    if ($1 == 0 && $2 > 0 && $2 == $3) print "checkpoint triple well-formed"
    else                                print "checkpoint triple malformed: " $0
    next
}
{ print }'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'one'),(2,'two'),(3,'three');
SELECT a, b FROM t ORDER BY a;
PRAGMA wal_checkpoint(PASSIVE);
SELECT a, b FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
