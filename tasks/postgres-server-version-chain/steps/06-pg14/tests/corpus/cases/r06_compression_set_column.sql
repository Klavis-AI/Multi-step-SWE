CREATE TABLE x06_54a(a text, b text);
ALTER TABLE x06_54a ALTER COLUMN a SET COMPRESSION pglz;
ALTER TABLE x06_54a ALTER COLUMN b SET COMPRESSION lz4;
SELECT attname, attcompression FROM pg_attribute
  WHERE attrelid='x06_54a'::regclass AND attnum>0 ORDER BY attnum;
ALTER TABLE x06_54a ALTER COLUMN b SET COMPRESSION default;
SELECT attname, attcompression FROM pg_attribute
  WHERE attrelid='x06_54a'::regclass AND attnum>0 ORDER BY attnum;
DROP TABLE x06_54a;
