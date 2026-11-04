-- R13.6: capture is off until it is asked for, and is never retroactive.
-- Neither the default state nor an explicit `off` creates the capture table,
-- so counting it in sqlite_schema returns 0 twice; after a capturing mode is
-- selected only the mutation that follows it is captured, and the two that
-- preceded it are not.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'before any pragma');
SELECT count(*) FROM sqlite_schema WHERE name='turso_cdc';
PRAGMA unstable_capture_data_changes_conn('off');
INSERT INTO t VALUES(2,'explicitly off');
SELECT count(*) FROM sqlite_schema WHERE name='turso_cdc';
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(3,'captured');
SELECT change_id, change_type, table_name, id FROM turso_cdc ORDER BY change_id;
SELECT count(*) FROM t;
