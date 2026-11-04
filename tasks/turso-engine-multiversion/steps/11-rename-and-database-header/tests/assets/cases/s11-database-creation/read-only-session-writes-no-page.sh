# R11.4: a session that only reads must not materialise page 1 in the main
# database file. Earlier checkpoints wrote a full 4096-byte page 1 the moment
# the file was opened, so a database that had never been written to was
# indistinguishable on disk from one that had been. Here the file is created
# by the open and is still empty when the process exits, and a foreign reader
# agrees it is an empty database.
#
# Existence is reported on its own line, separately from the length. The
# length is read through `wc -c < main.db`, and that redirection is what would
# fail -- with an empty field rather than a legible answer -- if a shell
# deferred creating the file until the first write. R11.4 states that opening
# a path that does not exist creates it, so the two halves are two lines.
#
# The second opinion here is stock sqlite3 reading the same file: `2>&1` on
# those two invocations is deliberate, so that a stock failure lands in the
# graded field and fails the case loudly instead of leaving a silent blank.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
SELECT 1;
SQL
if [ -e main.db ]; then echo "main_db_exists yes"; else echo "main_db_exists no"; fi
echo "main_db_bytes $(wc -c < main.db)"
echo "sqlite_tables $("$SQLITE3" -batch -list -noheader main.db 'SELECT count(*) FROM sqlite_schema;' 2>&1)"
echo "sqlite_integrity $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
