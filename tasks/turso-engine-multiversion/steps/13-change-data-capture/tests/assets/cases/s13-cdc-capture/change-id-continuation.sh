# R13.4's continuation rule: change_id carries on across an invocation
# boundary rather than restarting at 1.
#
# Arming capture a second time -- in a later invocation of the shell against
# the same file -- reuses the capture table that is already there and numbers
# the new rows after the ones it holds. The first invocation commits two
# capture rows; the second re-arms, updates and deletes, and must produce ids 3
# and 4 with the first two rows left exactly as they were. An engine that
# restarted the numbering at 1, or recreated the table, fails on the ids of the
# rows already committed rather than on any new one.
#
# The re-arm itself writes nothing -- the table is there, so the pragma is a
# setting and not a schema change, which is what the sibling
# rollback-leaves-file-bytes.sh measured on the file bytes.
#
# This is a shell case because it needs two invocations of the engine against
# one database file. Both invocations drop blank separator lines -- blank lines
# around result blocks come and go along the release chain and are not part of
# the contract -- and the second is folded with the standard error-report
# reduction as well, so the ids and the counts are the only things compared.
set -u
"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'a');
INSERT INTO t VALUES(2,'b');
SELECT change_id, change_type, id FROM turso_cdc ORDER BY change_id;
SQL
echo "second invocation:"
"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
PRAGMA unstable_capture_data_changes_conn('full');
UPDATE t SET v='z' WHERE id=1;
DELETE FROM t WHERE id=2;
SELECT change_id, change_type, id FROM turso_cdc ORDER BY change_id;
SELECT count(*) FROM turso_cdc;
SELECT id, v FROM t ORDER BY id;
SQL
