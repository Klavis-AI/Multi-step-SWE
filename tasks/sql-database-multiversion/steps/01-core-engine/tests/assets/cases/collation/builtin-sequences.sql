SELECT 'abc' = 'ABC', 'abc' = 'ABC' COLLATE NOCASE;
SELECT 'abc ' = 'abc', 'abc ' = 'abc' COLLATE RTRIM;
SELECT 'abc' = 'abc' COLLATE BINARY;
CREATE TABLE t(a TEXT COLLATE NOCASE);
INSERT INTO t VALUES('Hello');
SELECT count(*) FROM t WHERE a = 'hello';
SELECT count(*) FROM t WHERE a = 'hello' COLLATE BINARY;
