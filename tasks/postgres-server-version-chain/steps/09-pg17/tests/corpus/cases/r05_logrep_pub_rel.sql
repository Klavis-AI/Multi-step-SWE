CREATE TABLE x05_17a(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE x05_17a1 PARTITION OF x05_17a FOR VALUES FROM (0) TO (10);
CREATE PUBLICATION x05_17p FOR TABLE x05_17a WITH (publish_via_partition_root = true);
SELECT p.pubname, cl.relname, cl.relkind FROM pg_publication_rel r
  JOIN pg_publication p ON p.oid = r.prpubid
  JOIN pg_class cl ON cl.oid = r.prrelid
  WHERE p.pubname='x05_17p' ORDER BY cl.relname;
SELECT count(*) FROM pg_publication_rel r JOIN pg_publication p ON p.oid=r.prpubid
  WHERE p.pubname='x05_17p';
DROP PUBLICATION x05_17p;
DROP TABLE x05_17a;
