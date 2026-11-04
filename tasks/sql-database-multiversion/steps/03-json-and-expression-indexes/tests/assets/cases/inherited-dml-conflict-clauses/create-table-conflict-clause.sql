-- lang_createtable.html: "PRIMARY KEY, UNIQUE and NOT NULL constraints may be
-- explicitly assigned a default conflict resolution algorithm by including a
-- conflict-clause in their definitions.  Or, if a constraint definition does
-- not include a conflict-clause or it is a CHECK constraint, the default
-- conflict resolution algorithm is ABORT."  lang_conflict.html: "The syntax
-- for the ON CONFLICT clause is as shown above for the CREATE TABLE command",
-- "If a NOT NULL constraint violation occurs, the REPLACE conflict resolution
-- replaces the NULL value with the default value for that column", and "The
-- algorithm specified in the OR clause of an INSERT or UPDATE overrides any
-- algorithm specified in a CREATE TABLE."  PROFILE.md section 7 makes
-- lang_conflict.html normative for the five algorithms.
CREATE TABLE a1(x UNIQUE ON CONFLICT IGNORE);
CREATE TABLE a2(x UNIQUE ON CONFLICT REPLACE, y);
CREATE TABLE a3(x NOT NULL ON CONFLICT REPLACE DEFAULT 4, y);
CREATE TABLE a4(x, y, UNIQUE(x) ON CONFLICT IGNORE);
CREATE TABLE a5(x PRIMARY KEY ON CONFLICT REPLACE, y);
-- IGNORE on a column UNIQUE constraint: the second row is skipped, no error
INSERT INTO a1 VALUES(1);
INSERT INTO a1 VALUES(1);
SELECT count(*) FROM a1;
-- REPLACE on a column UNIQUE constraint: the pre-existing row is deleted
INSERT INTO a2 VALUES(1, 'p');
INSERT INTO a2 VALUES(1, 'q');
SELECT x, y FROM a2;
-- REPLACE on a NOT NULL constraint substitutes the column's default value
INSERT INTO a3 VALUES(NULL, 'p');
SELECT x, y FROM a3;
-- IGNORE on a table UNIQUE constraint
INSERT INTO a4 VALUES(1, 'p');
INSERT INTO a4 VALUES(1, 'q');
SELECT count(*) FROM a4;
-- REPLACE on a PRIMARY KEY constraint
INSERT INTO a5 VALUES(1, 'p');
INSERT INTO a5 VALUES(1, 'q');
SELECT x, y FROM a5;
-- the OR clause of an INSERT overrides the CREATE TABLE algorithm
INSERT OR IGNORE INTO a2 VALUES(1, 'r');
SELECT x, y FROM a2;
-- the stored schema text keeps the conflict-clause (PROFILE.md section 6)
SELECT sql FROM sqlite_master WHERE name = 'a3';
