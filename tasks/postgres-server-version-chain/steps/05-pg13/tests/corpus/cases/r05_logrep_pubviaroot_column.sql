SELECT attname, atttypid::regtype::text FROM pg_attribute
  WHERE attrelid='pg_publication'::regclass AND attname='pubviaroot';
SELECT count(*) FROM pg_attribute
  WHERE attrelid='pg_publication'::regclass AND attnum>0 AND attname LIKE 'pub%';
SELECT attname FROM pg_attribute
  WHERE attrelid='pg_publication'::regclass AND attnum>0 ORDER BY attnum;
