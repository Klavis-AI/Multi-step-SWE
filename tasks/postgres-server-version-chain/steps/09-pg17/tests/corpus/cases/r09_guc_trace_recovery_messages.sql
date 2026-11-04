SHOW trace_recovery_messages;
SELECT count(*) FROM pg_settings WHERE name = 'trace_recovery_messages';
SELECT current_setting('trace_recovery_messages', true) IS NULL;
SET trace_recovery_messages = debug1;
