CREATE TABLE r10vg3(a int, v int GENERATED ALWAYS AS (a * 2) VIRTUAL, s int GENERATED ALWAYS AS (a * 3) STORED);
INSERT INTO r10vg3(a) VALUES (2),(5);
SELECT a, v, s FROM r10vg3 ORDER BY a;
SELECT attname, attgenerated FROM pg_attribute WHERE attrelid = 'r10vg3'::regclass AND attnum > 0 ORDER BY attnum;
UPDATE r10vg3 SET a = 10 WHERE a = 2;
SELECT a, v, s FROM r10vg3 ORDER BY a;
DROP TABLE r10vg3;
