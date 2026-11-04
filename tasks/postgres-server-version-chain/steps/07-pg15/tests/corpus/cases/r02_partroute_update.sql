CREATE TABLE q2_rtu(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q2_rtu_1 PARTITION OF q2_rtu FOR VALUES FROM (1) TO (10);
CREATE TABLE q2_rtu_2 PARTITION OF q2_rtu FOR VALUES FROM (10) TO (20);
INSERT INTO q2_rtu VALUES (5,'x'),(15,'y');
UPDATE q2_rtu SET b = b || '!' WHERE a = 5;
UPDATE q2_rtu SET a = 7 WHERE a = 5;
UPDATE q2_rtu SET a = 15 WHERE a = 7;
SELECT tableoid::regclass::text AS part, a, b FROM q2_rtu ORDER BY a;
DROP TABLE q2_rtu;
