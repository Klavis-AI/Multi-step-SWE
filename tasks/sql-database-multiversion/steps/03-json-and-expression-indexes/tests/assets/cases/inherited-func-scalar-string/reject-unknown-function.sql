CREATE TABLE t(a);
INSERT INTO t VALUES(1);
SELECT no_such_function('x');
SELECT a FROM t;
