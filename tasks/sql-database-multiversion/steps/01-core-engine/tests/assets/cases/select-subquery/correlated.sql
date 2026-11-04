CREATE TABLE t(g TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('a',5),('b',2),('b',9);
SELECT g, v FROM t AS o WHERE v = (SELECT max(v) FROM t AS i WHERE i.g = o.g) ORDER BY g;
SELECT g, (SELECT count(*) FROM t AS i WHERE i.g = o.g) FROM t AS o WHERE v = 1 ORDER BY g;
