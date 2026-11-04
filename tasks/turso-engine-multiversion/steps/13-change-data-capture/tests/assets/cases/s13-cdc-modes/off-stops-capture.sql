-- R13.6 and R13.12: selecting `off` after a capturing mode has been in use
-- stops capture, and leaves the capture table and its rows in place.
--
-- The pragma is applied from the next statement onwards, so the INSERT issued
-- after the `off` writes no capture row while the row itself lands in the user
-- table, and the count of the capture table is unchanged. The table is still
-- there to be selected from afterwards -- `off` is a setting, not a schema
-- statement -- which is what the last two queries read: the count is still 1
-- and the table is still listed in `sqlite_schema`.
--
-- No images or ids are asserted here; this case is about which rows exist at
-- all after the mode is turned off.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'a');
SELECT count(*) FROM turso_cdc;
PRAGMA unstable_capture_data_changes_conn('off');
INSERT INTO t VALUES(2,'b');
SELECT count(*) FROM turso_cdc;
SELECT id, v FROM t ORDER BY id;
SELECT count(*) FROM sqlite_schema WHERE type = 'table' AND name = 'turso_cdc';
