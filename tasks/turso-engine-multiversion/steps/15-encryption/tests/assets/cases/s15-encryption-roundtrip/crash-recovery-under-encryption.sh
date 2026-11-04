# R15.10: a committed write made through an engaged cipher survives a process
# that never got to tidy up, and comes back whole rather than as a mixture.
#
# The three rows go in inside one explicit transaction, which is what makes
# "never a mixture" an assertion rather than a form of words. Three separate
# autocommits have no mixture to exclude -- every count from 0 to 3 would be
# some prefix of them committing, and all four are legal. One transaction has
# exactly two legal outcomes, the pre-transaction state (the table present and
# empty) and the post-transaction state (all three rows); a recovery that
# replayed part of the log would answer 1 or 2 and be caught here.
#
# The crash is simulated the way s01-wal-durability/reopen-after-simulated-
# crash simulates it, and the third bullet of spec/PROFILE.md's "The
# durability rule" section states the mechanism: the writer's database and its
# write-ahead log are copied away exactly as they lie on disk -- no
# checkpoint, no clean close of the copy -- and the copy is opened on its own,
# with the same cipher and key. The -shm file is deliberately not copied: it
# is derived state that a real crash would leave stale or absent, and the
# reader must rebuild it. Nothing here kills a process or depends on timing;
# the copy is what makes it deterministic.
#
# No checkpoint is run before the copy, and that is the point of the case
# rather than an oversight: the same durability rule that permits a writer to
# leave its committed rows in the log is what makes the db-plus-log pair the
# thing recovery has to cope with. What is asserted is what comes back out of
# the pair, never which half of it holds the rows.
#
# Stock sqlite3 is not a second reader here: it cannot read an encrypted
# database, so there is no second opinion to be had on what the copy recovers.
# That inability is a graded requirement in its own right (R15.8) and is
# asserted by the sibling case stock-sqlite3-rejects-encrypted-file.
set -u
key=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
"$CLI" -m "$CLI_MODE" -q enc.db > /dev/null 2>&1 <<SQL
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
CREATE TABLE c(id INTEGER PRIMARY KEY, s TEXT);
BEGIN;
INSERT INTO c VALUES(1,'committed-one');
INSERT INTO c VALUES(2,'committed-two');
INSERT INTO c VALUES(3,'committed-three');
COMMIT;
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM c;
SQL
mkdir -p crashed
cp enc.db crashed/enc.db
[ -e enc.db-wal ] && cp enc.db-wal crashed/enc.db-wal
echo "recovered with the key:"
( cd crashed && "$CLI" -m "$CLI_MODE" -q enc.db 2>/dev/null <<SQL
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
SELECT id, s FROM c ORDER BY id;
SELECT count(*) FROM c;
SQL
) | sed '/^[[:space:]]*$/d'
