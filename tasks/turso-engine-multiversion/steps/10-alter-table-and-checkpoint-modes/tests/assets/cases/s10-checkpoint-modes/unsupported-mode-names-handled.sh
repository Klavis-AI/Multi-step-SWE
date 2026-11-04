# R10.7: FULL, RESTART and TRUNCATE are accepted as mode *names* -- each is
# answered, the batch carries on, and the rows are intact afterwards. Before
# this checkpoint any mode argument aborts the process instead.
#
# What is graded is that the engine handles the name, not what it does with
# it. This checkpoint reports the three as unsupported and performs only
# PASSIVE; later checkpoints in this release chain perform them and answer a
# busy/frames/copied triple instead. Both are "handled", and the case folds
# them to the same token so that a step-10 expectation is not an assertion
# that the modes stay unimplemented. The distinction that IS graded is
# against the previous checkpoint, where `PRAGMA wal_checkpoint(FULL)` is not
# handled at all: the process aborts and prints nothing on stdout.
#
# Only the gutter and blank-line halves of spec/TEST_INTERFACE.md's "error
# report" fold are applied to the stream below; the `×` recognition is left to
# the `awk`, which counts a busy/frames/copied triple and a `×` report alike as
# one answer. Nothing after the marker reaches the comparison either way, so
# the wording is not graded here any more than it is under the sed fold.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d' | awk '
BEGIN { split("FULL RESTART TRUNCATE", mode, " ") }
/^[0-9]+\|[0-9]+\|[0-9]+$/ || /×/ {
    n++
    if (n <= 3) print mode[n] " handled"
    else        print "unexpected extra answer: " $0
    next
}
{ print }'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2),(3),(4);
PRAGMA wal_checkpoint(FULL);
PRAGMA wal_checkpoint(RESTART);
PRAGMA wal_checkpoint(TRUNCATE);
SELECT a FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
# Stock sqlite3 is the second opinion on "the name is handled": a .sh case is
# outside the generator's automatic comparison, so the case makes it itself.
# Stock 3.50.2 performs all three modes and answers a triple where this
# checkpoint reports them as unsupported -- and both are "handled", which is
# the whole of what R10.7 asks for here. The same classifier folds the two to
# the same token, so what is compared is that each name was answered, that the
# batch carried on, and that the rows are intact -- never what either engine
# did with the mode. Its stderr is merged on purpose: a stock-side failure must
# land in the graded output rather than vanish.
echo "stock_sqlite3:"
"$SQLITE3" -batch stock.db 'PRAGMA journal_mode=WAL;' > /dev/null 2>&1
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL' | awk '
BEGIN { split("FULL RESTART TRUNCATE", mode, " ") }
/^[0-9]+\|[0-9]+\|[0-9]+$/ || /×/ {
    n++
    if (n <= 3) print mode[n] " handled"
    else        print "unexpected extra answer: " $0
    next
}
{ print }'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2),(3),(4);
PRAGMA wal_checkpoint(FULL);
PRAGMA wal_checkpoint(RESTART);
PRAGMA wal_checkpoint(TRUNCATE);
SELECT a FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
