CREATE TABLE x05_45a(a int);
CREATE INDEX x05_45i ON x05_45a(a) WITH (deduplicate_items = off);
SELECT cl.relname, am.amname, cl.reloptions FROM pg_class cl
  JOIN pg_am am ON am.oid = cl.relam WHERE cl.relname='x05_45i';
SELECT indexdef FROM pg_indexes WHERE indexname='x05_45i';
SELECT unnest(reloptions) AS opt FROM pg_class WHERE relname='x05_45i' ORDER BY opt;
DROP TABLE x05_45a;
