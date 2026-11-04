-- R13.13: the capture table is an ordinary table with a fixed leading column
-- set.
-- pragma_table_info reports cids 0 to 6 as change_id, change_time,
-- change_type, table_name, id, before and after, in that order, and
-- sqlite_schema carries it as a `table` row whose name and tbl_name are both
-- the capture table's. Later checkpoints append further columns, so only the
-- leading seven are asserted.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'a');
SELECT cid, name FROM pragma_table_info('turso_cdc') WHERE cid <= 6 ORDER BY cid;
SELECT type, name, tbl_name FROM sqlite_schema WHERE name='turso_cdc';
