SELECT name, vartype, enumvals FROM pg_settings WHERE name='wal_compression';
SELECT name, vartype, enumvals FROM pg_settings WHERE name='recovery_prefetch';
SELECT count(*) FROM pg_settings WHERE name='default_toast_compression';
SELECT boot_val FROM pg_settings WHERE name='wal_compression';
