CREATE TABLE q4_cwp(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q4_cwp_1 PARTITION OF q4_cwp FOR VALUES FROM (1) TO (10);
CREATE TABLE q4_cwp_2 PARTITION OF q4_cwp FOR VALUES FROM (10) TO (20);
COPY q4_cwp FROM stdin WHERE a >= 10;
1	one
11	eleven
15	fifteen
\.
SELECT tableoid::regclass::text AS part, a, b FROM q4_cwp ORDER BY a;
SELECT count(*) FROM q4_cwp_1;
DROP TABLE q4_cwp;
