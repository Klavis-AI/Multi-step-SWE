CREATE TABLE q4_gct(a int, b int GENERATED ALWAYS AS (a * 2) STORED) PARTITION BY RANGE (a);
CREATE TABLE q4_gct_1 PARTITION OF q4_gct FOR VALUES FROM (1) TO (10);
INSERT INTO q4_gct(a) VALUES (1),(5);
SELECT tableoid::regclass::text AS part, a, b FROM q4_gct ORDER BY a;
SELECT attname, attgenerated FROM pg_attribute
  WHERE attrelid = 'q4_gct_1'::regclass AND attnum > 0 ORDER BY attnum;
DROP TABLE q4_gct;
