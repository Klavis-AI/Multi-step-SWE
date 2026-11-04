CREATE TABLE q2_prx(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q2_prx_1 PARTITION OF q2_prx FOR VALUES FROM (1) TO (10);
INSERT INTO q2_prx VALUES (99,'too big');
INSERT INTO q2_prx VALUES (0,'too small');
INSERT INTO q2_prx VALUES (NULL,'null key');
INSERT INTO q2_prx VALUES (5,'fits');
SELECT a, b FROM q2_prx ORDER BY a;
DROP TABLE q2_prx;
