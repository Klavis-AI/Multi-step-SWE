CREATE TABLE x05_11a(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE x05_11a1 PARTITION OF x05_11a FOR VALUES FROM (0) TO (10);
CREATE TABLE x05_11a2 PARTITION OF x05_11a FOR VALUES FROM (10) TO (20);
CREATE PUBLICATION x05_11p FOR TABLE x05_11a WITH (publish_via_partition_root = true);
SELECT pubname, pubviaroot FROM pg_publication WHERE pubname='x05_11p';
SELECT pubname, schemaname, tablename FROM pg_publication_tables
  WHERE pubname='x05_11p' ORDER BY tablename;
DROP PUBLICATION x05_11p;
DROP TABLE x05_11a;
