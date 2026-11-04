CREATE TABLE t1(a, b);
CREATE INDEX i1 ON t1(a);
CREATE TABLE t2(x);
DROP TABLE t1;
SELECT type, name FROM sqlite_master ORDER BY name;
DROP TABLE IF EXISTS nosuch;
SELECT count(*) FROM sqlite_master;
DROP TABLE t2;
SELECT count(*) FROM sqlite_master;
