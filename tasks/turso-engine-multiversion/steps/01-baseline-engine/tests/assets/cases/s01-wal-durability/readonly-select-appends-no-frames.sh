# R01.11, second consequence: a connection that only reads changes nothing.
#
# `spec/PROFILE.md`'s durability rule says a read-only connection leaves both
# `main.db` and `main.db-wal` byte-identical -- no frame appended for a read,
# and no rewrite of the log header on open. This case fingerprints both files
# after the writer has gone, runs an invocation that is nothing but SELECTs,
# and compares.
#
# No stock sqlite3 cross-read, for the same reason as the case beside it:
# stock sqlite3 checkpoints and unlinks the `-wal` when the last connection
# closes, so it has no `-wal` to leave unchanged and cannot express this
# observation at all. The profile names the divergence explicitly.
# A read appends nothing. The -wal file is fingerprinted after the writer has
# gone, a second invocation runs nothing but SELECTs, and the fingerprint has
# to be the same afterwards. A engine that logs a frame for a read, or that
# rewrites the log header on every open, changes it.
#
# This is the profile's durability rule, second consequence: "a connection
# that only reads changes nothing ... no frame is appended for a read, and the
# log header is not rewritten on open". The other behaviour stock sqlite3
# exhibits -- checkpoint and unlink the -wal when the last connection closes --
# is explicitly not what this engine does, so a note that a SQLite-shaped
# implementation would fail here is a statement about stock sqlite3, not about
# an undocumented obligation on this engine.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE r(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO r VALUES(1,'alpha');
INSERT INTO r VALUES(2,'bravo');
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
SELECT count(*) FROM r;
SQL
# The comparison below is only meaningful if there is a -wal to compare. With
# `set -u` and no `set -e`, an absent main.db-wal would leave both digests the
# empty string and the case would report "yes" for a comparison of nothing
# against nothing -- an engine that checkpointed and unlinked the log would
# pass. Its presence is therefore asserted first, on its own line.
if [ -e main.db-wal ]; then echo "wal-present-before-read: yes"; else echo "wal-present-before-read: no"; fi
before="$(sha256sum < main.db-wal | cut -d' ' -f1)"
beforedb="$(sha256sum < main.db | cut -d' ' -f1)"
echo "read-only invocation:"
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed -e '/^[[:space:]]*$/d'
SELECT id, s FROM r ORDER BY id;
SELECT count(*), max(id) FROM r;
SQL
after="$(sha256sum < main.db-wal | cut -d' ' -f1)"
afterdb="$(sha256sum < main.db | cut -d' ' -f1)"
[ "$before" = "$after" ] && echo "wal-unchanged-by-read: yes" || echo "wal-unchanged-by-read: no"
[ "$beforedb" = "$afterdb" ] && echo "db-unchanged-by-read: yes" || echo "db-unchanged-by-read: no"
