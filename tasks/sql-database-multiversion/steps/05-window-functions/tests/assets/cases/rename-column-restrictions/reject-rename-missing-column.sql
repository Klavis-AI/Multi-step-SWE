-- PROFILE.md 5.11: RENAME COLUMN naming a column the table does not have is
-- rejected -- "the page defines the statement as changing 'the column-name of
-- table table-name', and there is no such column to change" -- and the schema
-- is left exactly as it was.
CREATE TABLE t(k INTEGER PRIMARY KEY, alpha TEXT, n INTEGER);
INSERT INTO t VALUES(1,'x',10);
-- a column name the table never had
ALTER TABLE t RENAME COLUMN nosuch TO beta;
-- a column that belongs to a different table
CREATE TABLE u(gamma TEXT);
ALTER TABLE t RENAME COLUMN gamma TO beta;
-- a name that is no longer a column, because it was just renamed away
ALTER TABLE t RENAME COLUMN alpha TO beta;
SELECT sql FROM sqlite_master WHERE name = 't';
ALTER TABLE t RENAME COLUMN alpha TO omega;
-- the schema is exactly what the successful rename left
SELECT sql FROM sqlite_master WHERE name = 't';
PRAGMA table_info(t);
SELECT k, beta, n FROM t ORDER BY k;
PRAGMA integrity_check;
