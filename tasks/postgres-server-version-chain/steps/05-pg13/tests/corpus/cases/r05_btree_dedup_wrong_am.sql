CREATE TABLE x05_44a(a int, b text);
INSERT INTO x05_44a VALUES (1,'x');
CREATE INDEX x05_44i1 ON x05_44a USING hash (a) WITH (deduplicate_items = on);
CREATE INDEX x05_44i2 ON x05_44a USING gin (to_tsvector('simple', b)) WITH (deduplicate_items = on);
CREATE INDEX x05_44i3 ON x05_44a(a) WITH (deduplicate_items = maybe);
CREATE INDEX x05_44i4 ON x05_44a(a) WITH (deduplicate_item = on);
SELECT indexname FROM pg_indexes WHERE tablename='x05_44a' ORDER BY indexname;
DROP TABLE x05_44a;
