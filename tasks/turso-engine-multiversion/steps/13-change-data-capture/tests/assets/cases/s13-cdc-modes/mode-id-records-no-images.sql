-- R13.12: `id` mode records the row that changed and neither of its images.
-- Each of the three mutation kinds still produces exactly one capture row
-- carrying the table name and the rowid, and typeof() reports null for both
-- the before and the after column on every one of them.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('id');
INSERT INTO t VALUES(1,'a');
UPDATE t SET v='b' WHERE id=1;
DELETE FROM t WHERE id=1;
SELECT change_id, change_type, table_name, id, typeof(before), typeof(after) FROM turso_cdc ORDER BY change_id;
