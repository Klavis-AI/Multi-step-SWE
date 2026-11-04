-- PROFILE.md 5.9: after the rename the column is known by its new name "and by
-- its old name nowhere -- a statement naming the old name is rejected exactly
-- as one naming a column that never existed is".  Each rejected statement
-- leaves the database exactly as it was (TEST_INTERFACE.md 2.3).
CREATE TABLE t(k INTEGER PRIMARY KEY, old TEXT, n INTEGER);
INSERT INTO t(k,old,n) VALUES(1,'x',10),(2,'y',20);
ALTER TABLE t RENAME COLUMN old TO new;
SELECT k, new, n FROM t ORDER BY k;
-- a result column naming the old name
SELECT old FROM t;
-- a WHERE clause naming it
SELECT k FROM t WHERE old = 'x';
-- an ORDER BY naming it
SELECT k FROM t ORDER BY old;
-- an INSERT column list naming it
INSERT INTO t(k,old,n) VALUES(3,'z',30);
-- an UPDATE SET naming it
UPDATE t SET old = 'q' WHERE k = 1;
-- a name the table never had is rejected the same way
SELECT nosuch FROM t;
-- nothing above changed the table
SELECT k, new, n FROM t ORDER BY k;
SELECT count(*) FROM t;
PRAGMA integrity_check;
