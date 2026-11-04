# R10.8: hex() on blobs with bytes >= 0x80 renders their correct two-digit
# uppercase encoding, both for literals and for blobs read back from a table,
# and stock sqlite3 renders the same strings for the same statements.
#
# hex() on blobs with bytes >= 0x80 must render their correct two-digit
# uppercase hex encoding. Earlier checkpoints corrupted those bytes to the
# UTF-8 replacement sequence EFBFBD, which is itself indistinguishable from
# a real EF BF BD byte sequence -- so the case pins the low bytes too.
#
# Every value graded here is hex() over a BLOB, which is what R10.8 states and
# all it states. An earlier version of this case also asserted
# `SELECT hex(char(233));` = C3A9 -- hex() applied to a TEXT value, produced by
# char(). That was removed in round 3 and must not come back. char() appears in
# no requirement of any step in this package and nowhere else in the corpus, so
# an engine that implements every documented requirement of steps 1 through 10
# and has no char() lost this whole group -- weight 0.158333 -- for conforming.
# The UTF-8 encoding it demonstrated is already asserted one line above by
# `SELECT hex(x'C3A9');`, on a blob literal, where R10.8 puts it. If a later
# round wants hex() over text graded, state it in a requirement first.
#
# .sh rather than .sql because this checkpoint prints a blank separator line
# before every result block. Being a .sh case it is outside the generator's
# automatic stock-sqlite3 comparison, so the last lines make that comparison
# themselves.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
SELECT hex(x'00417F80FF');
SELECT hex(x'C3A9');
SELECT length(x'80FF');
CREATE TABLE t(b BLOB);
INSERT INTO t VALUES(x'80'),(x'FF'),(x'007F80FF');
SELECT hex(b) FROM t ORDER BY rowid;
SELECT count(*) FROM t;
SQL
# Stock sqlite3 runs the same statements on its own file. Its stderr is merged
# on purpose: a stock-side failure must land in the graded output rather than
# vanish.
echo "stock_sqlite3:"
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL'
SELECT hex(x'00417F80FF');
SELECT hex(x'C3A9');
SELECT length(x'80FF');
CREATE TABLE t(b BLOB);
INSERT INTO t VALUES(x'80'),(x'FF'),(x'007F80FF');
SELECT hex(b) FROM t ORDER BY rowid;
SELECT count(*) FROM t;
SQL
