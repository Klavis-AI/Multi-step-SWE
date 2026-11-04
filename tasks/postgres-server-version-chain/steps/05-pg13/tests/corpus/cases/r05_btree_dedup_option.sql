CREATE TABLE x05_42a(a int, b text);
INSERT INTO x05_42a SELECT g%7, 't'||(g%5) FROM generate_series(1,300) g;
CREATE INDEX x05_42i1 ON x05_42a(a) WITH (deduplicate_items = off);
CREATE INDEX x05_42i2 ON x05_42a(b) WITH (deduplicate_items = on);
SELECT relname, reloptions FROM pg_class
  WHERE relname IN ('x05_42i1','x05_42i2') ORDER BY relname;
SELECT count(*) FROM x05_42a WHERE a = 3;
DROP TABLE x05_42a;
