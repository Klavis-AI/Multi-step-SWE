
-- R12.1: what PRAGMA schema_version answers on a database with no schema, after
-- each CREATE TABLE, and after an INSERT that is not a schema change. The same
-- script is run through stock sqlite3 and the two answers must agree.
-- PRAGMA schema_version reads the page-1 schema cookie. It must start at zero
-- on a database with no schema and move with each schema change, so that a
-- caller can tell whether a cached schema is still valid.
PRAGMA schema_version;
CREATE TABLE a(x);
PRAGMA schema_version;
CREATE TABLE b(y);
PRAGMA schema_version;
INSERT INTO b VALUES(1);
PRAGMA schema_version;
SELECT count(*) FROM a;
SELECT count(*) FROM b;
