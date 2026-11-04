# R02.1 and R02.2: a checkpoint moves frames out of the log and into the
# database file; it must
# not move a single row. The same battery of queries is run immediately before
# and immediately after the checkpoint in one connection, and the two blocks
# have to be identical -- which is asserted here by comparing them, and shown
# by printing the block itself so that a wrong-but-equal pair of blocks cannot
# pass unnoticed. The checkpoint's own answer is reduced to "did it answer
# with a triple", because the frame counts differ from release to release and
# are graded elsewhere.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt 2>/dev/null <<'SQL'
CREATE TABLE c(id INTEGER PRIMARY KEY, k TEXT, v INTEGER);
INSERT INTO c VALUES(1,'a',10);
INSERT INTO c VALUES(2,'b',20);
INSERT INTO c VALUES(3,'a',30);
SELECT 'MARK';
SELECT id, k, v FROM c ORDER BY id;
SELECT k, sum(v) FROM c GROUP BY k ORDER BY k;
SELECT count(*), min(id), max(id) FROM c;
SELECT 'MARK';
PRAGMA wal_checkpoint;
SELECT 'MARK';
SELECT id, k, v FROM c ORDER BY id;
SELECT k, sum(v) FROM c GROUP BY k ORDER BY k;
SELECT count(*), min(id), max(id) FROM c;
SELECT 'MARK';
SQL
sed -e '/^[[:space:]]*$/d' raw.txt > lines.txt
: > before.txt
: > during.txt
: > after.txt
awk '/^MARK$/ { n++; next }
     n == 1 { print >> "before.txt" }
     n == 2 { print >> "during.txt" }
     n == 3 { print >> "after.txt" }' lines.txt
echo "before:"
cat before.txt
echo "after:"
cat after.txt
awk -F'|' 'END { printf "checkpoint-answered-with-a-triple: %s\n",
    (NR == 1 && NF == 3 && $1 ~ /^[0-9]+$/ && $2 ~ /^[0-9]+$/ && $3 ~ /^[0-9]+$/) ? "yes" : "no" }' during.txt
if cmp -s before.txt after.txt; then
    echo "checkpoint-changed-nothing: yes"
else
    echo "checkpoint-changed-nothing: no"
fi
# A second implementation reads the same file, so the blocks above are not
# asserted on this engine's word alone. Note what this does and does not show:
# stock sqlite3 opens main.db with the -wal still in place, so it may be
# reading *through* the log rather than out of the main file. It is therefore
# NOT R02.1's "the frames are in the database file" half -- that half is
# R02.4's, and the sibling case checkpointed-db-reads-without-wal.sh is what
# grades it, by deleting -wal and -shm before stock is allowed near the file.
# What this read does establish is that the file and log this engine wrote are
# in stock's own format and that a second implementation traversing them
# reaches the same answers.
#
# The query repeats all three statements of the before/after blocks, not just
# the row listing: the GROUP BY sum and the count/min/max would otherwise be
# compared only against this engine's own answer, printed twice.
# Measured stable at every checkpoint from this one onward.
echo "stock-sees:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id, k, v FROM c ORDER BY id;
     SELECT k, sum(v) FROM c GROUP BY k ORDER BY k;
     SELECT count(*), min(id), max(id) FROM c;' 2>/dev/null
