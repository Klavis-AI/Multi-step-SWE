SHOW stats_temp_directory;
SET stats_temp_directory = '/tmp';
SELECT count(*) FROM pg_settings WHERE name='stats_temp_directory';
SELECT current_setting('stats_temp_directory', true) IS NULL;
