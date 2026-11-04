CREATE TABLE q2_prm(a int, b int, t text) PARTITION BY RANGE (a, b);
CREATE TABLE q2_prm_1 PARTITION OF q2_prm FOR VALUES FROM (1, 1) TO (1, 10);
CREATE TABLE q2_prm_2 PARTITION OF q2_prm FOR VALUES FROM (1, 10) TO (2, 5);
INSERT INTO q2_prm VALUES (1,1,'a'),(1,9,'b'),(1,10,'c'),(2,4,'d');
SELECT tableoid::regclass::text AS part, a, b, t FROM q2_prm ORDER BY a, b;
INSERT INTO q2_prm VALUES (2,5,'e');
SELECT pg_get_partkeydef('q2_prm'::regclass);
DROP TABLE q2_prm;
