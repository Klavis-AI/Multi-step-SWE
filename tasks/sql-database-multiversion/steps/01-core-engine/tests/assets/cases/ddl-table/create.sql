create table t1(a INTEGER, b TEXT);
CREATE TABLE IF NOT EXISTS t1(zzz);
CREATE TABLE "t 2"(x, y);
CREATE TABLE t3(a NOT NULL DEFAULT 7, b DEFAULT 'hi', c);
INSERT INTO t3 DEFAULT VALUES;
SELECT a, b, c IS NULL FROM t3;
SELECT type, name, tbl_name, sql FROM sqlite_master ORDER BY name;
