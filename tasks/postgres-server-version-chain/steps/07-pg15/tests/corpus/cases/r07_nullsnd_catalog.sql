CREATE TABLE x07_61a(a int, b int);
CREATE UNIQUE INDEX x07_61i ON x07_61a(a) NULLS NOT DISTINCT;
CREATE UNIQUE INDEX x07_61j ON x07_61a(b);
SELECT indexname, indexdef FROM pg_indexes
  WHERE tablename='x07_61a' ORDER BY indexname;
SELECT count(*) FROM pg_attribute WHERE attrelid='pg_index'::regclass
  AND attname='indnullsnotdistinct';
SELECT c.relname, i.indnullsnotdistinct FROM pg_index i
  JOIN pg_class c ON c.oid = i.indexrelid
  WHERE c.relname IN ('x07_61i','x07_61j') ORDER BY c.relname;
DROP TABLE x07_61a;
