# R08.3: a DESC index stores its keys descending -- a forward scan of it yields
# descending values, and stock sqlite3 walking the file the engine wrote finds
# the index in the order the schema text declares.
# R08.7: a predicate selecting only keys below every stored key, and one
# selecting only keys above every stored key, each return no rows rather than
# the nearest stored entry.
#
# What the lines observe. `range:` is a bounded range scan over the DESC
# index; `first:` is the same index answering ORDER BY b DESC LIMIT 1, so the
# largest key must come out first; `scan:` is the whole index read forward,
# which adds the one key `range:` excludes and is therefore not a restatement
# of it. `below:` and `above:` are R08.7: each prints the row count and the
# extreme value the query found, so an engine that clamped an off-end seek to
# the first or last stored entry would print `1/10` or `1/40` there instead of
# `0/none`. `barrier:` is the last line of the engine block and shows that
# batch ran to the end; the two lines after it come from other observers.
#
# `engine-file-integrity:` is the line that reaches the *storage* half of
# R08.3, and it is stock sqlite3's verdict on the file this engine wrote --
# not on the stock-built database the block below it uses. Every result-row
# line above is produced under ORDER BY b DESC, so on their own they cannot
# tell an engine that stores the index descending from one that stores it
# ascending and sorts afterwards: both print the same bytes. Stock sqlite3
# can tell, because PRAGMA integrity_check walks each index against its table
# using the key order the schema text declares.
#
# Measured on the reference chain, 2026-09-21: at v0.0.20 and at every later
# checkpoint through v0.1.5 the line reads `ok`; at v0.0.19, which parses DESC
# and then stores ascending, it reads `row 1 missing from index i_desc` with
# two more lines like it. The check is not vacuous: a stock-built control --
# a database whose schema row says `b DESC` over an index b-tree holding
# ascending keys, assembled by copying the index page between two stock files
# -- is reported the same way, so an ascending store under a DESC declaration
# is what the line detects rather than something incidental.
#
# What is still not graded is the *plan*. No line here asserts that the scan
# happened without a sort step: nothing the shell prints on stdout says which
# access path answered a query, and this step's contract says so in terms.
# R08.3 is graded on the two things that are observable -- the order the rows
# come out in, and the order the keys are stored in.
#
# The off-end seeks already answer correctly at v0.0.19; they are graded here
# because this step's contract states them, and the group as a whole still
# discriminates on the ordering and storage lines above.
#
# The second opinion. This is a .sh case, so it does not get the automatic
# stock-sqlite3 differential that .sql cases get. Every statement above is
# ordinary SQL with an answer stock sqlite3 can also produce, so the case runs
# the identical batch against a stock-built database of its own and prints
# stock's answers under `sqlite-` labels. The two blocks must read the same;
# a divergence lands in the graded output rather than passing unseen.
set -u

# Indexes are opt-in from v0.0.22 to v0.1.3 and the switch changes shape
# across that span, so ask the shell what it accepts rather than assuming. An
# engine with no such option gets no option.
IDX=""
case "$("$CLI" --help 2>&1)" in
  *"--experimental-indexes <"*) IDX="--experimental-indexes=true" ;;
  *"--experimental-indexes"*)   IDX="--experimental-indexes" ;;
esac

"$CLI" -m "$CLI_MODE" $IDX -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
CREATE TABLE t(a INTEGER, b INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30),(4,40);
CREATE INDEX i_desc ON t(b DESC);
SELECT 'range:'||b FROM t WHERE b>=20 ORDER BY b DESC;
SELECT 'first:'||b FROM t ORDER BY b DESC LIMIT 1;
SELECT 'scan:'||b FROM t ORDER BY b DESC;
SELECT 'below:'||count(*)||'/'||coalesce(max(b),'none') FROM t WHERE b<5;
SELECT 'above:'||count(*)||'/'||coalesce(max(b),'none') FROM t WHERE b>100;
SELECT 'barrier:'||count(*) FROM t;
SQL

echo "engine-file-integrity:$("$SQLITE3" -batch -list -noheader main.db 'PRAGMA integrity_check;' 2>&1)"

"$SQLITE3" -batch -list -noheader -separator '|' stock.db 2>&1 <<'SQL' | sed 's/^/sqlite-/'
CREATE TABLE t(a INTEGER, b INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30),(4,40);
CREATE INDEX i_desc ON t(b DESC);
SELECT 'range:'||b FROM t WHERE b>=20 ORDER BY b DESC;
SELECT 'first:'||b FROM t ORDER BY b DESC LIMIT 1;
SELECT 'scan:'||b FROM t ORDER BY b DESC;
SELECT 'below:'||count(*)||'/'||coalesce(max(b),'none') FROM t WHERE b<5;
SELECT 'above:'||count(*)||'/'||coalesce(max(b),'none') FROM t WHERE b>100;
SELECT 'barrier:'||count(*) FROM t;
SQL
