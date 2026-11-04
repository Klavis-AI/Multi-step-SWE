SHOW force_parallel_mode;
SELECT count(*) FROM pg_settings WHERE name = 'force_parallel_mode';
SET force_parallel_mode = on;
SELECT current_setting('force_parallel_mode');
SELECT current_setting('force_parallel_mode', true) IS NULL;
