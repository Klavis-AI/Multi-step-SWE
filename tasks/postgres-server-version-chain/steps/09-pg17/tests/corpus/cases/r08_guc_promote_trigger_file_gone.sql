SHOW promote_trigger_file;
SELECT count(*) FROM pg_settings WHERE name = 'promote_trigger_file';
SELECT current_setting('promote_trigger_file', true) IS NULL;
SET promote_trigger_file = '/tmp/nope';
