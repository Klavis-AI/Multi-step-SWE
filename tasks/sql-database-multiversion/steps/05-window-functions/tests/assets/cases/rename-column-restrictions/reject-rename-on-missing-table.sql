-- PROFILE.md 5.11: ALTER TABLE naming a table that does not exist is rejected,
-- and "the schema is left exactly as it was: sqlite_master and PRAGMA
-- table_info report what they reported before, and the old column name still
-- works."  (TEST_INTERFACE.md 2.3 -- the message text is never compared.)
CREATE TABLE t(k INTEGER PRIMARY KEY, alpha TEXT);
INSERT INTO t VALUES(1,'x');
SELECT name, sql FROM sqlite_master ORDER BY name;
-- a table that was never created
ALTER TABLE nosuch RENAME COLUMN alpha TO beta;
-- a name that belongs to an index, not a table
CREATE INDEX i1 ON t(alpha);
ALTER TABLE i1 RENAME COLUMN alpha TO beta;
-- the schema is exactly what it was, and the column keeps its name
SELECT name, sql FROM sqlite_master ORDER BY name;
PRAGMA table_info(t);
SELECT k, alpha FROM t ORDER BY k;
-- and a rename that names a table that DOES exist still works, so this case
-- observes the capability itself and not only a rejection that a release
-- without RENAME COLUMN would produce just as well (PROFILE.md 5.9, 5.10).
ALTER TABLE t RENAME COLUMN alpha TO beta;
SELECT k, beta FROM t ORDER BY k;
SELECT sql FROM sqlite_master WHERE name = 't';
PRAGMA integrity_check;
