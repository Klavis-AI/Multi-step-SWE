CREATE TABLE q4_pti(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q4_pti_1 PARTITION OF q4_pti FOR VALUES FROM (1) TO (10);
CREATE INDEX q4_pti_b ON q4_pti (b);
SELECT relid::regclass::text, isleaf, level FROM pg_partition_tree('q4_pti_b')
  ORDER BY relid::regclass::text;
SELECT pg_partition_root('q4_pti_1_b_idx')::text;
DROP TABLE q4_pti;
