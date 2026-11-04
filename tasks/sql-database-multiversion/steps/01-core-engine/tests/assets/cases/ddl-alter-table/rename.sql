CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
CREATE INDEX ib ON t(b);
INSERT INTO t VALUES(1,'p'),(2,'q');
ALTER TABLE t RENAME TO u;
SELECT a, b FROM u ORDER BY a;
SELECT type, name, tbl_name FROM sqlite_master ORDER BY name;
INSERT INTO u VALUES(3,'r');
SELECT count(*) FROM u WHERE b = 'r';
