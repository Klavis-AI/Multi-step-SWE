SELECT t FROM (VALUES ('b'),('A'),('a'),('B'),('_a'),('1')) v(t) ORDER BY t;
SELECT x FROM (VALUES (1),(NULL),(2)) v(x) ORDER BY x NULLS FIRST;
SELECT x FROM (VALUES (1),(NULL),(2)) v(x) ORDER BY x DESC;
SELECT t FROM (VALUES ('10'),('9'),('100')) v(t) ORDER BY t;
SELECT i FROM (VALUES (10),(9),(100)) v(i) ORDER BY i;
