CREATE TABLE q4_ptd(a int) PARTITION BY RANGE (a);
CREATE TABLE q4_ptd_1 PARTITION OF q4_ptd FOR VALUES FROM (1) TO (10);
CREATE TABLE q4_ptd_2 PARTITION OF q4_ptd FOR VALUES FROM (10) TO (20);
SELECT count(*) FROM pg_partition_tree('q4_ptd');
ALTER TABLE q4_ptd DETACH PARTITION q4_ptd_2;
SELECT relid::regclass::text, isleaf FROM pg_partition_tree('q4_ptd') ORDER BY 1;
SELECT pg_partition_root('q4_ptd_2')::text;
DROP TABLE q4_ptd, q4_ptd_2;
