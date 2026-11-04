SELECT 1, 'a', NULL, 1+1 AS two, 'x' "quoted col";
SELECT 1 AS a, 2 AS a;
SELECT * FROM (VALUES (1,'a'),(2,'b')) v(i,t) ORDER BY i;
SELECT i FROM (VALUES (1),(2),(3)) v(i) WHERE i > 1 ORDER BY i DESC;
SELECT DISTINCT x FROM (VALUES (1),(1),(2)) v(x) ORDER BY x;
SELECT DISTINCT ON (g) g, v FROM (VALUES (1,10),(1,20),(2,5)) t(g,v) ORDER BY g, v;
