# R11.4 and R11.7: page 1 appears when the first write needs it, and not
# before. The file is measured after a read-only session and again after a
# session that creates a table, in two separate processes, so what is observed
# is what survives process exit rather than what is in some cache.
#
# What this case deliberately does NOT assert is the *length* of the file
# after the write. TEST_INTERFACE.md's "Not scored" list leaves file size in
# bytes to the writer, and spec/PROFILE.md's durability rule lets a conforming
# engine leave committed pages in `main.db-wal` until a checkpoint folds them
# in -- so a byte count here would fail an engine whose every row and every
# byte is correct. An earlier version of this case compared `wc -c` to 8192
# and did exactly that.
#
# The header fields are read after an explicit `PRAGMA wal_checkpoint` for the
# same reason. The checkpoint is the one documented way to *require* the fold
# (spec/PROFILE.md, "The durability rule"), so after it every committed page
# is in `main.db` whichever policy the engine follows, and the in-header
# database size at offset 28 is then a fact about the database rather than
# about when this engine happens to checkpoint. Two pages is what the workload
# allocates and R11.7 states the rule: page 1 carries the header and
# sqlite_schema, the one table gets one root page, and its two rows fit in
# that page.
#
# The magic at offset 0 is the assertion that page 1 materialised at all -- a
# main.db still at zero length yields an empty field there and fails the line.
# Offsets 16 and 18..19 are the page size and the WAL read/write version bytes
# spec/PROFILE.md rule 1 fixes; nothing else in the header is read, because
# nothing else in it is chosen by a document (see the field-by-field note in
# s01-file-interop/page1-format-fields.sh).
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
SELECT 1;
SQL
echo "after_read_only $(wc -c < main.db)"
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2);
PRAGMA wal_checkpoint;
-- Durability barrier: spec/PROFILE.md's durability rule puts the obligation on
-- the writing invocation, so this invocation ends by reading back every table
-- it wrote before the process exits. Reading the data pages is what completes
-- the fold; reading only sqlite_schema touches page 1 and is not sufficient.
SELECT count(*) FROM t;
SQL
hex() { od -An -tx1 -j "$1" -N "$2" main.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//'; }
echo "header_magic $(od -An -c -j 0 -N 15 main.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')"
echo "page_size $(hex 16 2)"
echo "write_read_version $(hex 18 2)"
echo "db_size_pages $(hex 28 4)"
echo "rows $("$SQLITE3" -batch -list -noheader main.db 'SELECT count(*) FROM t;' 2>&1)"
