CREATE TABLE x05_10a(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE x05_10a1 PARTITION OF x05_10a FOR VALUES FROM (0) TO (10);
CREATE TABLE x05_10a2 PARTITION OF x05_10a FOR VALUES FROM (10) TO (20);
CREATE PUBLICATION x05_10p FOR TABLE x05_10a;
SELECT pubname, pubviaroot FROM pg_publication WHERE pubname='x05_10p';
SELECT pubname, schemaname, tablename FROM pg_publication_tables
  WHERE pubname='x05_10p' ORDER BY tablename;
DROP PUBLICATION x05_10p;
DROP TABLE x05_10a;
