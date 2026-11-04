CREATE TABLE q3_pmn(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q3_pmn_1 PARTITION OF q3_pmn FOR VALUES FROM (1) TO (10);
CREATE TABLE q3_pmn_2 PARTITION OF q3_pmn FOR VALUES FROM (10) TO (20);
INSERT INTO q3_pmn VALUES (5,'x');
UPDATE q3_pmn SET a = 99 WHERE a = 5;
UPDATE q3_pmn SET a = NULL WHERE a = 5;
SELECT tableoid::regclass::text AS part, a, b FROM q3_pmn ORDER BY a;
DROP TABLE q3_pmn;
