
-- R12.1: the spacing of the increments, not only their direction -- zero with
-- no schema, one per CREATE TABLE, and unmoved by an INSERT. Stock sqlite3 runs
-- the same script and must produce the same sequence.
-- The schema cookie is the page-1 field a reader trusts to tell whether its
-- cached schema is stale. It is zero on a database with no schema and one per
-- CREATE TABLE after that; an INSERT is not a schema change and must not move
-- it. The values here are compared with stock sqlite3's answer for the same
-- script, so the spacing of the increments is graded and not only their
-- direction.
--
-- DROP TABLE is deliberately absent: it does not move the cookie at this
-- checkpoint but does from v0.1.3, so a case pinning it would regress later.
PRAGMA schema_version;
CREATE TABLE a(x);
PRAGMA schema_version;
CREATE TABLE b(y);
PRAGMA schema_version;
INSERT INTO b VALUES(1);
PRAGMA schema_version;
CREATE TABLE c(z);
PRAGMA schema_version;
SELECT count(*) FROM a;
SELECT count(*) FROM b;
SELECT count(*) FROM c;
