CREATE TABLE q2_rto(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q2_rto_1 PARTITION OF q2_rto FOR VALUES FROM (1) TO (5);
CREATE TABLE q2_rto_2 PARTITION OF q2_rto FOR VALUES FROM (5) TO (9);
INSERT INTO q2_rto SELECT g, 'v' || g FROM generate_series(1,8) g;
SELECT tableoid::regclass::text AS part, count(*) FROM q2_rto GROUP BY 1 ORDER BY 1;
SELECT a FROM q2_rto WHERE tableoid = 'q2_rto_2'::regclass ORDER BY a;
SELECT tableoid = 'q2_rto_1'::regclass AS in_first FROM q2_rto WHERE a = 3;
DROP TABLE q2_rto;
