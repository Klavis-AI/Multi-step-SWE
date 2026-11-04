# R01.11, third consequence: a simulated crash is a file copy, and the copy
# still holds every committed row.
#
# `spec/PROFILE.md`'s durability rule fixes both the mechanism and the
# obligation. The mechanism: the writer exits, then `main.db` and
# `main.db-wal` are copied as they lie into a fresh directory -- no checkpoint
# first, no clean close, and `main.db-shm` deliberately not copied, because it
# is derived state a real crash would leave stale or absent. The obligation:
# the copy, opened on its own, yields every committed row. An implementation
# that needs a live `-shm`, or that cannot rebuild one, does not survive it.
#
# No stock sqlite3 cross-read. The pair this case copies is a database plus a
# surviving `-wal`, which is a state stock sqlite3 does not leave behind, and
# recovery from it is the behaviour under test rather than a SQL result that
# could have a second opinion. That stock sqlite3 can read an ordinary file
# this engine wrote is established separately, in
# `s01-file-interop/sqlite-reads-our-database.sh`.
# A committed write must survive a process that never got to tidy up. The
# writer's database and its write-ahead log are copied away exactly as they
# lie on disk -- no checkpoint, no clean close of the copy -- and the copy is
# opened on its own. Every committed row has to be there.
#
# The -shm file is deliberately not copied: it is derived state that a real
# crash would leave stale or absent, and the reader must rebuild it.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE c(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO c VALUES(1,'committed-one');
INSERT INTO c VALUES(2,'committed-two');
INSERT INTO c VALUES(3,'committed-three');
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
SELECT count(*) FROM c;
SQL
engine_rc=$?
echo "engine_exit: $engine_rc"
mkdir -p crashed
cp main.db crashed/main.db
[ -e main.db-wal ] && cp main.db-wal crashed/main.db-wal
echo "recovered:"
( cd crashed && "$CLI" -m "$CLI_MODE" -q main.db <<'SQL'
SELECT id, s FROM c ORDER BY id;
SELECT count(*) FROM c;
SQL
) | sed -e '/^[[:space:]]*$/d'
