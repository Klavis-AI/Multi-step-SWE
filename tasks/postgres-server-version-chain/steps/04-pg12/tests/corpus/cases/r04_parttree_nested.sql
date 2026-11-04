CREATE TABLE q4_ptn(a int, b int) PARTITION BY RANGE (a);
CREATE TABLE q4_ptn_1 PARTITION OF q4_ptn FOR VALUES FROM (1) TO (10) PARTITION BY RANGE (b);
CREATE TABLE q4_ptn_1a PARTITION OF q4_ptn_1 FOR VALUES FROM (1) TO (5);
SELECT relid::regclass::text, parentrelid::regclass::text, isleaf, level
  FROM pg_partition_tree('q4_ptn') ORDER BY level, relid::regclass::text;
SELECT pg_partition_root('q4_ptn_1a')::text;
SELECT relid::regclass::text FROM pg_partition_ancestors('q4_ptn_1a') ORDER BY 1;
DROP TABLE q4_ptn;
