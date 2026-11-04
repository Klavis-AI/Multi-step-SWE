CREATE TABLE q3_dfr(a int) PARTITION BY RANGE (a);
CREATE TABLE q3_dfr_1 PARTITION OF q3_dfr FOR VALUES FROM (1) TO (10);
CREATE TABLE q3_dfr_d PARTITION OF q3_dfr DEFAULT;
INSERT INTO q3_dfr VALUES (5),(50),(-5),(NULL);
SELECT tableoid::regclass::text AS part, a FROM q3_dfr ORDER BY a NULLS LAST;
SELECT relname, pg_get_expr(relpartbound, oid) FROM pg_class WHERE relname = 'q3_dfr_d';
DROP TABLE q3_dfr;
