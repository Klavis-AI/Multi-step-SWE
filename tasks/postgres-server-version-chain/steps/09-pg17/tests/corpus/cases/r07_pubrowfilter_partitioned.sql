CREATE TABLE x07_23a(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE x07_23a1 PARTITION OF x07_23a FOR VALUES FROM (0) TO (10);
CREATE PUBLICATION x07_23p FOR TABLE x07_23a WHERE (a > 2)
  WITH (publish_via_partition_root = true);
SELECT pubname, tablename, rowfilter FROM pg_publication_tables
  WHERE pubname='x07_23p' ORDER BY tablename;
ALTER PUBLICATION x07_23p SET (publish_via_partition_root = false);
SELECT pubname, tablename, rowfilter FROM pg_publication_tables
  WHERE pubname='x07_23p' ORDER BY tablename;
DROP PUBLICATION x07_23p;
DROP TABLE x07_23a;
