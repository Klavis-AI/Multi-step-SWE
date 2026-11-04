CREATE TABLE x05_14a(a int primary key, b text);
CREATE PUBLICATION x05_14p FOR TABLE x05_14a
  WITH (publish = 'insert, update', publish_via_partition_root = false);
SELECT pubname, pubinsert, pubupdate, pubdelete, pubtruncate, pubviaroot
  FROM pg_publication WHERE pubname='x05_14p';
ALTER PUBLICATION x05_14p SET (publish = 'insert');
SELECT pubname, pubinsert, pubupdate, pubdelete, pubtruncate
  FROM pg_publication WHERE pubname='x05_14p';
DROP PUBLICATION x05_14p;
DROP TABLE x05_14a;
