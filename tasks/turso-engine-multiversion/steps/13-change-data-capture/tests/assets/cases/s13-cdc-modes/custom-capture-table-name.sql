-- R13.11: the pragma's `<mode>,<name>` form names the capture table.
-- With `full,audit_log` the captured rows arrive in `audit_log`, and the
-- schema holds that table and the user table under the names the script
-- chose, with no table called `turso_cdc`. The schema query names the three
-- tables it asks about rather than listing the schema, so an implementation
-- that also creates bookkeeping tables of its own is not failed for them.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full,audit_log');
INSERT INTO t VALUES(1,'a');
UPDATE t SET v='b' WHERE id=1;
SELECT type, name FROM sqlite_schema WHERE name IN ('t','audit_log','turso_cdc') ORDER BY name;
SELECT change_id, change_type, table_name, id FROM audit_log ORDER BY change_id;
