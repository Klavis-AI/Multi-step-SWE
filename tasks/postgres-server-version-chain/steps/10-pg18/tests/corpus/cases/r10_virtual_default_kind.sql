CREATE TABLE r10vg2(a int, b int GENERATED ALWAYS AS (a + 1));
INSERT INTO r10vg2(a) VALUES (7);
SELECT a, b FROM r10vg2 ORDER BY a;
SELECT attname, attgenerated FROM pg_attribute WHERE attrelid = 'r10vg2'::regclass AND attnum > 0 ORDER BY attnum;
SELECT pg_get_expr(adbin, adrelid) FROM pg_attrdef WHERE adrelid = 'r10vg2'::regclass ORDER BY adnum;
DROP TABLE r10vg2;
