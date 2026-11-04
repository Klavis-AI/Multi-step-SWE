CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
SELECT total_changes();
INSERT INTO t VALUES(1,'p'),(2,'q'),(3,'r');
SELECT changes(), total_changes(), last_insert_rowid();
UPDATE t SET b = 'z' WHERE a >= 2;
SELECT changes(), total_changes(), last_insert_rowid();
DELETE FROM t WHERE a = 1;
SELECT changes(), total_changes();
SELECT sqlite_version();
