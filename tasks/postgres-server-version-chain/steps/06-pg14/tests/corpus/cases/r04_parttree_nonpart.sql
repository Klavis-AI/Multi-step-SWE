CREATE TABLE q4_ptx(a int);
SELECT count(*) FROM pg_partition_tree('q4_ptx');
SELECT pg_partition_root('q4_ptx') IS NULL AS root_null;
SELECT count(*) FROM pg_partition_ancestors('q4_ptx');
SELECT count(*) FROM pg_partition_tree('nosuchrel_q4_ptx');
DROP TABLE q4_ptx;
