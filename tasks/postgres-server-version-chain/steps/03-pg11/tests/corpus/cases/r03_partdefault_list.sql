CREATE TABLE q3_dfl(a int, b text) PARTITION BY LIST (a);
CREATE TABLE q3_dfl_1 PARTITION OF q3_dfl FOR VALUES IN (1,2);
CREATE TABLE q3_dfl_d PARTITION OF q3_dfl DEFAULT;
INSERT INTO q3_dfl VALUES (1,'one'),(2,'two'),(99,'other'),(NULL,'null');
SELECT tableoid::regclass::text AS part, a, b FROM q3_dfl ORDER BY a NULLS LAST;
SELECT count(*) FROM q3_dfl_d;
DROP TABLE q3_dfl;
