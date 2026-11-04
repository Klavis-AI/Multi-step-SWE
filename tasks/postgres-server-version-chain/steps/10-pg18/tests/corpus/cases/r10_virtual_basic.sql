CREATE TABLE r10vg1(a int, b int GENERATED ALWAYS AS (a * 2) VIRTUAL);
INSERT INTO r10vg1(a) VALUES (3),(5);
SELECT a, b FROM r10vg1 ORDER BY a;
SELECT attname, attgenerated FROM pg_attribute WHERE attrelid = 'r10vg1'::regclass AND attnum > 0 ORDER BY attnum;
DROP TABLE r10vg1;
