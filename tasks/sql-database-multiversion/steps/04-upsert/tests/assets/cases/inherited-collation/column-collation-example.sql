CREATE TABLE t1(
  x INTEGER PRIMARY KEY,
  a,
  b COLLATE BINARY,
  c COLLATE RTRIM,
  d COLLATE NOCASE
);
INSERT INTO t1 VALUES(1,'abc','abc', 'abc ','abc');
INSERT INTO t1 VALUES(2,'abc','abc', 'abc', 'ABC');
INSERT INTO t1 VALUES(3,'abc','abc', 'abc ', 'Abc');
INSERT INTO t1 VALUES(4,'abc','abc ','ABC', 'abc');
SELECT x FROM t1 WHERE a = b ORDER BY x;
SELECT x FROM t1 WHERE a = b COLLATE RTRIM ORDER BY x;
SELECT x FROM t1 WHERE d = a ORDER BY x;
