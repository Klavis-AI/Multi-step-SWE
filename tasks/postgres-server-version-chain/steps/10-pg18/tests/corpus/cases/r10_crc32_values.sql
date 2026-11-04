SELECT crc32('abc'::bytea), crc32c('abc'::bytea);
SELECT crc32(''::bytea), crc32c(''::bytea);
SELECT crc32('\x00ff'::bytea), crc32c('\x00ff'::bytea);
SELECT pg_typeof(crc32('a'::bytea));
