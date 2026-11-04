CREATE TABLE t(a);
CREATE INDEX i ON t(a);
CREATE TABLE u(b UNIQUE);
SELECT name FROM sqlite_master ORDER BY name;
DROP TABLE t;
SELECT name FROM sqlite_master ORDER BY name;
DROP TABLE u;
SELECT count(*) FROM sqlite_master;
