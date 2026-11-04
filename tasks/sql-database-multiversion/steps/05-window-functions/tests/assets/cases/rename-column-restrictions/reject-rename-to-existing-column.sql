-- PROFILE.md 5.11: a RENAME COLUMN whose new name is already the name of
-- another column of the same table is rejected -- "a table cannot have two
-- columns called the same thing" (lang_createtable.html, step 1 sections 1
-- and 5) -- and the schema is left exactly as it was.
CREATE TABLE t(k INTEGER PRIMARY KEY, alpha TEXT, n INTEGER);
INSERT INTO t VALUES(1,'x',10);
-- the new name is another column of the same table
ALTER TABLE t RENAME COLUMN alpha TO n;
-- ... including the table's own primary key column
ALTER TABLE t RENAME COLUMN alpha TO k;
-- the schema and the data are exactly what they were
SELECT sql FROM sqlite_master WHERE name = 't';
PRAGMA table_info(t);
SELECT k, alpha, n FROM t ORDER BY k;
-- a name no other column uses is accepted, and the same rename then repeats
-- as a rejection because the name is now taken
ALTER TABLE t RENAME COLUMN alpha TO beta;
SELECT sql FROM sqlite_master WHERE name = 't';
ALTER TABLE t RENAME COLUMN n TO beta;
SELECT sql FROM sqlite_master WHERE name = 't';
PRAGMA table_info(t);
PRAGMA integrity_check;
