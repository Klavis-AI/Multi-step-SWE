CREATE TABLE x05_46a(a int, b int);
CREATE UNIQUE INDEX x05_46i ON x05_46a(a) WITH (deduplicate_items = on);
INSERT INTO x05_46a SELECT g, g FROM generate_series(1,50) g;
INSERT INTO x05_46a VALUES (1, 999);
SELECT count(*), min(a), max(a) FROM x05_46a;
DELETE FROM x05_46a WHERE a % 2 = 0;
SELECT count(*) FROM x05_46a;
SELECT a FROM x05_46a WHERE a < 8 ORDER BY a;
DROP TABLE x05_46a;
