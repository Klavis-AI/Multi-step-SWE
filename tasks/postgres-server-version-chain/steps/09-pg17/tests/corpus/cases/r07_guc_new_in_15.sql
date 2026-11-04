SHOW recovery_prefetch;
SELECT count(*) FROM pg_settings WHERE name IN
  ('shared_memory_size','recovery_prefetch','wal_decode_buffer_size');
SELECT name, vartype, context FROM pg_settings
  WHERE name IN ('recovery_prefetch','wal_decode_buffer_size') ORDER BY name;
SHOW wal_compression;
