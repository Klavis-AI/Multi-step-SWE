-- R13.8, R13.10, R13.14: capture continues across an ALTER TABLE, and each
-- ALTER is itself captured.
-- After ADD COLUMN the later INSERT and UPDATE are still captured and their
-- after images are records over three columns rather than two; after
-- DROP COLUMN the next INSERT's image is back to two, because an image is
-- encoded against the column set the table has at the moment of the mutation.
-- Each ALTER rewrites the table's sqlite_schema row and produces one capture
-- row of its own whose table_name is `sqlite_schema`. That row is read here by
-- name rather than being left to be inferred from the gaps in the user table's
-- change ids: the second SELECT asks for the rows the requirement names, and
-- the gaps -- user rows 1, 3, 4 and 6 against a total of 6 -- are what those
-- two rows cost.
-- Only change_id and table_name are read from the ALTER's own row. Its images
-- hold the schema row's text and its `id` is that row's rowid, neither of
-- which any requirement fixes.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'a');
ALTER TABLE t ADD COLUMN extra INTEGER;
INSERT INTO t VALUES(2,'b',99);
UPDATE t SET extra=7 WHERE id=1;
ALTER TABLE t DROP COLUMN extra;
INSERT INTO t VALUES(3,'c');
SELECT change_id, change_type, table_name, id, hex(after) FROM turso_cdc WHERE table_name='t' ORDER BY change_id;
SELECT change_id, table_name FROM turso_cdc WHERE table_name='sqlite_schema' ORDER BY change_id;
SELECT count(*) FROM turso_cdc;
SELECT id, v FROM t ORDER BY id;
