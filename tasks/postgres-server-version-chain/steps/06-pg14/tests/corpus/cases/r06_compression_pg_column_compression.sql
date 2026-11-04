CREATE TABLE x06_55a(a text, b text);
ALTER TABLE x06_55a ALTER COLUMN a SET COMPRESSION pglz;
ALTER TABLE x06_55a ALTER COLUMN b SET COMPRESSION lz4;
INSERT INTO x06_55a SELECT repeat('abcdefgh',1000), repeat('abcdefgh',1000);
SELECT pg_column_compression(a), pg_column_compression(b) FROM x06_55a;
SELECT length(a), length(b) FROM x06_55a;
SELECT pg_column_compression('short'::text) IS NULL;
DROP TABLE x06_55a;
