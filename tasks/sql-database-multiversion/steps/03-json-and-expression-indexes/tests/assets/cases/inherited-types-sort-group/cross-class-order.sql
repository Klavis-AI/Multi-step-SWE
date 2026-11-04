CREATE TABLE t(v);
INSERT INTO t VALUES(NULL),(2),(1.5),('b'),('a'),(x'01'),(x'00');
SELECT typeof(v), CASE typeof(v) WHEN 'blob' THEN hex(v) ELSE v END FROM t ORDER BY v;
