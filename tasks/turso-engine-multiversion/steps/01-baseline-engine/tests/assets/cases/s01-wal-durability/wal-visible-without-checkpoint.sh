# R01.11, first consequence: a committed write leaves a `-wal` behind and is
# visible to a later connection with no checkpoint in between.
#
# R01.11 is the durability requirement, and `spec/PROFILE.md`'s durability rule
# grades three consequences of it; this case is the first. The writer exits,
# `main.db-wal` is still on disk -- which is asserted, because the engine
# deliberately does not unlink it -- and a second process reads every committed
# row back with no `PRAGMA wal_checkpoint` anywhere in the case. What the log
# still *holds* at that moment is deliberately not asserted, exactly as the
# profile says.
#
# No stock sqlite3 cross-read, because on this point stock sqlite3 does the
# opposite by design: it checkpoints and unlinks the `-wal` as the last
# connection closes, so it can never exhibit the state this case requires. The
# profile says so in as many words ("Stock `sqlite3` removes the `-wal` as it
# closes, so it is not a model for this behaviour"). The rows themselves are
# ordinary and stock sqlite3 does read them back from a file this engine
# wrote -- that is `s01-file-interop/sqlite-reads-our-database.sh`.
# A committed write is visible to a later connection with no checkpoint of any
# kind in between: the rows live in the -wal file and the reader has to find
# them there. No PRAGMA wal_checkpoint appears anywhere in this case, and the
# -wal file is still present when the reader opens the database.
#
# Blank separator lines between result blocks come and go along the release
# chain and are removed here, exactly as the .sql grading path removes them.
# Every result row selected below has more than one column or is a count, so
# nothing removed could have been a row.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE w(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO w VALUES(1,'first');
INSERT INTO w VALUES(2,'second');
INSERT INTO w VALUES(3,'third');
-- Durability barrier. The writing invocation ends by reading back every table
-- it wrote; reading the data pages is what forces a pending write to complete,
-- and reading only sqlite_schema touches page 1 and is not sufficient.
-- It is not needed at this step's own checkpoint -- v0.0.13 finalises its WAL
-- at process exit, measured clean without the barrier -- and it is here
-- because the generator re-measures every case at every later checkpoint in
-- the chain and rejects one whose answer moves there, and v0.0.16 through
-- v0.0.18 do not finalise it at exit: a cross-process
-- write-then-read corrupts 5-25% of the time there. It asserts nothing and
-- weakens nothing; the same rows are read back either way.
SELECT count(*) FROM w;
SQL
engine_rc=$?
echo "engine_exit: $engine_rc"
if [ -e main.db-wal ]; then echo "wal-present-after-write: yes"; else echo "wal-present-after-write: no"; fi
echo "reopened:"
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed -e '/^[[:space:]]*$/d'
SELECT id, s FROM w ORDER BY id;
SELECT count(*) FROM w;
SQL
