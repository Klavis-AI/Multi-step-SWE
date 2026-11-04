-- R14.10: `turso_cdc` carries an eighth column named `updates`, populated for an UPDATE and NULL for an INSERT and a DELETE.
-- The capture table gains an eighth column at this checkpoint. It is
-- populated only for updates, where it holds the patch record of the columns
-- the update named, and stays NULL for inserts and deletes.
--
-- The three updates below are the discriminating set. The first changes a
-- stored value. The second, `SET a=a`, names a column without changing what
-- is stored, and its patch record must still flag that column -- the flags
-- record what the SET list named, not what changed value. The third moves the
-- row's rowid, which is not captured as an update at all but as a delete
-- followed by an insert, so neither of those two rows carries a patch record.
-- Both hex values asserted below are the layout `TEST_INTERFACE.md` decodes
-- byte by byte; neither is an opaque constant.
CREATE TABLE t(id INTEGER PRIMARY KEY, a TEXT, b TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'x','y');
UPDATE t SET b='z' WHERE id=1;
UPDATE t SET a=a WHERE id=1;
UPDATE t SET id=2 WHERE id=1;
DELETE FROM t WHERE id=2;
SELECT cid, name FROM pragma_table_info('turso_cdc') ORDER BY cid;
SELECT change_id, change_type, table_name, id, typeof(updates), hex(updates) FROM turso_cdc ORDER BY change_id;
