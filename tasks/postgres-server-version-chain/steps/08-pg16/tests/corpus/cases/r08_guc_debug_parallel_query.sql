SHOW debug_parallel_query;
SELECT name, setting, vartype, boot_val FROM pg_settings WHERE name = 'debug_parallel_query';
SELECT current_setting('debug_parallel_query');
