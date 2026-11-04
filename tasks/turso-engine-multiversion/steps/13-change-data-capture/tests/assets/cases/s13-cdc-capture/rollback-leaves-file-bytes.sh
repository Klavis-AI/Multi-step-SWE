# R13.5: a rolled-back transaction leaves the database file and its
# write-ahead log byte-identical to their pre-transaction bytes.
# The sibling .sql case shows the rolled-back rows are gone from the result
# rows. This one grades the other half of the same requirement, the one a
# query cannot see: a transaction that is rolled back must not leave its
# pages behind on disk either. main.db and main.db-wal are snapshotted after
# one invocation has written and exited, a second invocation runs and rolls
# back a transaction, and the two snapshots are compared.
#
# Only the verdict is printed, never a hash: change_time holds a wall-clock
# value, so the bytes of the file differ from one run to the next while the
# question this case asks -- did they change across the rolled-back
# transaction -- does not.
#
# Presence is asserted separately from identity, and before it. `state` reports
# a file that is not there as `absent`, and two absent readings compare equal,
# so the identity check on its own would be satisfied by a log that was never
# written -- it would report `unchanged` about nothing. spec/PROFILE.md's
# durability rule says the log is still in the directory after a writing
# invocation at every checkpoint on the chain, and that only its length varies,
# so this case requires both files to exist after the rolled-back invocation as
# well as to be unchanged across it. An absent log now fails on the presence
# line instead of passing on the identity one.
set -u

state() {
    if [ -f "$1" ]; then sha256sum < "$1" | cut -d' ' -f1; else echo absent; fi
}

present() {
    if [ -f "$1" ]; then echo "$1: present"; else echo "$1: absent"; fi
}

# The writing invocation reads back what it wrote before it exits, as the
# durability rule requires of a writer.
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>/dev/null <<'SQL'
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'kept');
SELECT count(*) FROM t;
SELECT count(*) FROM turso_cdc;
SQL

db_before=$(state main.db)
wal_before=$(state main.db-wal)

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL'
PRAGMA unstable_capture_data_changes_conn('full');
BEGIN;
INSERT INTO t VALUES(2,'rolled back');
UPDATE t SET v='also rolled back' WHERE id=1;
ROLLBACK;
SELECT change_id, change_type, table_name, id FROM turso_cdc ORDER BY change_id;
SELECT id, v FROM t ORDER BY id;
SQL

db_after=$(state main.db)
wal_after=$(state main.db-wal)

present main.db
present main.db-wal
if [ "$db_before" = "$db_after" ]; then echo "main.db: unchanged"; else echo "main.db: changed"; fi
if [ "$wal_before" = "$wal_after" ]; then echo "main.db-wal: unchanged"; else echo "main.db-wal: changed"; fi

# Stock sqlite3's second opinion on the state the rollback left behind.
echo "stock:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id,v FROM t ORDER BY id;' 2>&1
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT count(*) FROM turso_cdc;' 2>&1
