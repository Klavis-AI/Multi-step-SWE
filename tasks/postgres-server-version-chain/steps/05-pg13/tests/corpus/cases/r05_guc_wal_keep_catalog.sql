SELECT name, unit, context, vartype FROM pg_settings
  WHERE name IN ('wal_keep_size','wal_keep_segments') ORDER BY name;
SELECT count(*) FROM pg_settings WHERE name='wal_keep_size';
SELECT count(*) FROM pg_settings WHERE name='wal_keep_segments';
SELECT count(*) FROM pg_settings WHERE name LIKE 'wal_keep%';
