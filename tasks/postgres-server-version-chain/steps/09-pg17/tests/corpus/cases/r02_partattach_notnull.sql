CREATE TABLE q2_pan(a int, b text) PARTITION BY RANGE (a);
SELECT attname, attnotnull FROM pg_attribute
  WHERE attrelid = 'q2_pan'::regclass AND attnum > 0 ORDER BY attnum;
CREATE TABLE q2_pan_1 PARTITION OF q2_pan FOR VALUES FROM (1) TO (10);
SELECT attname, attnotnull FROM pg_attribute
  WHERE attrelid = 'q2_pan_1'::regclass AND attnum > 0 ORDER BY attnum;
ALTER TABLE q2_pan_1 ALTER COLUMN a DROP NOT NULL;
INSERT INTO q2_pan_1 VALUES (NULL,'direct');
ALTER TABLE q2_pan DROP COLUMN a;
DROP TABLE q2_pan;
