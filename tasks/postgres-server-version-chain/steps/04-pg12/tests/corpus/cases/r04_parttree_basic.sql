CREATE TABLE q4_ptb(a int) PARTITION BY RANGE (a);
CREATE TABLE q4_ptb_1 PARTITION OF q4_ptb FOR VALUES FROM (1) TO (10);
CREATE TABLE q4_ptb_2 PARTITION OF q4_ptb FOR VALUES FROM (10) TO (20);
SELECT relid::regclass::text, parentrelid::regclass::text, isleaf, level
  FROM pg_partition_tree('q4_ptb') ORDER BY relid::regclass::text;
SELECT count(*) FROM pg_partition_tree('q4_ptb_1');
DROP TABLE q4_ptb;
