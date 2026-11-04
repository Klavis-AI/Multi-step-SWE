CREATE TABLE r10vgd(a int);
INSERT INTO r10vgd VALUES (4);
ALTER TABLE r10vgd ADD COLUMN b int GENERATED ALWAYS AS (a * 5) VIRTUAL;
SELECT a, b FROM r10vgd ORDER BY a;
ALTER TABLE r10vgd ALTER COLUMN a TYPE bigint;
SELECT a, b FROM r10vgd ORDER BY a;
ALTER TABLE r10vgd DROP COLUMN b;
SELECT attname, attgenerated FROM pg_attribute WHERE attrelid = 'r10vgd'::regclass AND attnum > 0 AND NOT attisdropped ORDER BY attnum;
DROP TABLE r10vgd;
