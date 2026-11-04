-- R14.11: a schema mutation made while capture is on writes its own `turso_cdc` row against `sqlite_schema`, ahead of the data rows.
-- From this checkpoint the write path captures mutations of sqlite_schema as
-- well, so a statement that writes a schema row produces its own capture row
-- against the schema table, before any row of the object it created is
-- captured. All three change_type values reach sqlite_schema here and the case
-- exercises one of each: CREATE TABLE and CREATE INDEX are captured as inserts
-- (change_type 1), ALTER TABLE ... ADD COLUMN as an update (change_type 0,
-- carrying a patch record over the schema row's own columns, which is why
-- typeof(updates) is read), and DROP TABLE as a delete (change_type -1). At
-- the previous checkpoint the same script captured only the data row.
--
-- Every rowid this pins is fixed by the step's documents rather than by the
-- reference. The pragma materialises the capture table immediately, so
-- `turso_cdc` takes the first sqlite_schema rowid and each object created
-- after it takes the next: `t` is 2, `ix` is 3, `gone` is 4. That rowid is the
-- `id` each schema capture row carries, which is why the ADD COLUMN update
-- names 2 -- it rewrites `t`'s own schema row -- and the drop names 4.
--
-- `gone` is dropped rather than `t` on purpose. Dropping a table that has an
-- index removes both schema rows but is captured as a single delete, for the
-- table's row only; that asymmetry is an engine choice this step does not fix,
-- so the drop asserted here is of an index-free table, where the one delete
-- row is unambiguous. `TEST_INTERFACE.md` lists the indexed case among what
-- this milestone does not grade.
--
-- The trailing count is five, not six: the capture table's own creation is not
-- itself captured.
PRAGMA unstable_capture_data_changes_conn('full');
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
CREATE INDEX ix ON t(v);
INSERT INTO t VALUES(1,'a');
ALTER TABLE t ADD COLUMN extra INTEGER;
CREATE TABLE gone(id INTEGER PRIMARY KEY);
DROP TABLE gone;
SELECT change_id, change_type, table_name, id, typeof(updates) FROM turso_cdc ORDER BY change_id;
SELECT count(*) FROM turso_cdc WHERE table_name='sqlite_schema';
