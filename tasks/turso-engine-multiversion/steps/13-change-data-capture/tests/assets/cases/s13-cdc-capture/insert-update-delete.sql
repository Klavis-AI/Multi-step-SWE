-- R13.1, R13.2, R13.3: one capture row per mutated row, with the change_type
-- that names the mutation.
-- Two INSERTs, an UPDATE and a DELETE against one table produce four capture
-- rows and no more, carrying change_type 1, 1, 0 and -1, the mutated table's
-- name, and each mutated row's rowid; the table's own rows are read back
-- afterwards to show the mutations themselves landed.
-- Capture is switched on after the table exists so that the capture of schema
-- mutations does not enter this case. change_time holds a wall-clock value
-- and is never selected.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'one');
INSERT INTO t VALUES(2,'two');
UPDATE t SET v='TWO' WHERE id=2;
DELETE FROM t WHERE id=1;
SELECT change_id, change_type, table_name, id FROM turso_cdc ORDER BY change_id;
SELECT id, v FROM t ORDER BY id;
