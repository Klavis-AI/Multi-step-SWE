# R10.7: the mode keyword is case-insensitive -- PASSIVE, passive and PaSsIvE
# are all accepted and all report the same well-formed busy|frames|copied
# triple for the same WAL state.
#
# The mode keyword is a SQL keyword, so its spelling is case-insensitive, and
# the accepted form returns SQLite's busy/frames/copied triple. Three
# spellings of the same mode must all be accepted and all report the same
# triple for the same WAL state -- repeated checkpoints over an already-copied
# WAL report the same frame and copied counts rather than zero.
#
# The triple is graded by shape and by equality across the three spellings,
# not by value: busy must be 0, the frame count positive and equal to the
# copied count, and the second and third triples byte-identical to the first.
# The exact frame count depends on how many pages the engine wrote, which the
# contract lists as not scored, so it is not pinned here.
#
# Only PASSIVE is asserted here. FULL, RESTART and TRUNCATE are accepted as
# mode names at this checkpoint but are not performed as those modes; later
# checkpoints implement them, so pinning their triple here would pin a value
# that changes. That each of them is handled without a panic is the separate
# case unsupported-mode-names-handled.sh.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d' | awk -F'|' '
/^[0-9]+\|[0-9]+\|[0-9]+$/ {
    n++
    if (!($1 == 0 && $2 > 0 && $2 == $3)) { print "triple " n " malformed: " $0; next }
    if (n == 1) { first = $0; print "triple 1 well-formed"; next }
    if ($0 == first) print "triple " n " equals triple 1"
    else             print "triple " n " differs from triple 1: " $0
    next
}
{ print }'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2),(3),(4);
PRAGMA wal_checkpoint(PASSIVE);
PRAGMA wal_checkpoint(passive);
PRAGMA wal_checkpoint(PaSsIvE);
SELECT count(*) FROM t;
SQL
# Stock sqlite3 is the second opinion on the case-insensitivity: a .sh case is
# outside the generator's automatic comparison, so the case makes it itself.
# The mode is a SQL keyword in both implementations, so all three spellings
# must be accepted by both, and each must answer the same triple as the first
# for the same WAL state. Stock is compared by the same shape-and-equality rule
# the engine is graded by and never by value -- the frame count is page
# accounting and the two need not agree on it. Its stderr is merged on purpose:
# a stock-side failure must land in the graded output rather than vanish.
echo "stock_sqlite3:"
"$SQLITE3" -batch stock.db 'PRAGMA journal_mode=WAL;' > /dev/null 2>&1
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL' | awk -F'|' '
/^[0-9]+\|[0-9]+\|[0-9]+$/ {
    n++
    if (!($1 == 0 && $2 > 0 && $2 == $3)) { print "triple " n " malformed: " $0; next }
    if (n == 1) { first = $0; print "triple 1 well-formed"; next }
    if ($0 == first) print "triple " n " equals triple 1"
    else             print "triple " n " differs from triple 1: " $0
    next
}
{ print }'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2),(3),(4);
PRAGMA wal_checkpoint(PASSIVE);
PRAGMA wal_checkpoint(passive);
PRAGMA wal_checkpoint(PaSsIvE);
SELECT count(*) FROM t;
SQL
