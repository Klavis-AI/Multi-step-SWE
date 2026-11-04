-- R13.1, R13.2, R13.3, R13.10: which of the two images each mutation kind
-- keeps, and the bytes of the images themselves.
-- An INSERT leaves before NULL and after a blob, a DELETE the reverse, an
-- UPDATE both; hex() then reads each image out byte for byte and shows it is
-- the SQLite record of the row over the table's columns, with the INTEGER
-- PRIMARY KEY column carrying its value rather than the NULL an on-disk table
-- row stores for a rowid alias. The images are rendered as hex so that the
-- assertion is over bytes rather than over a blob rendering.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'ab');
UPDATE t SET v='cd' WHERE id=1;
DELETE FROM t WHERE id=1;
SELECT change_id, change_type, typeof(before), typeof(after) FROM turso_cdc ORDER BY change_id;
SELECT change_id, hex(before), hex(after) FROM turso_cdc ORDER BY change_id;
