SHOW db_user_namespace;
SELECT count(*) FROM pg_settings WHERE name = 'db_user_namespace';
SELECT current_setting('db_user_namespace', true) IS NULL;
SET db_user_namespace = on;
