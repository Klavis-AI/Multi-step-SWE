SELECT name FROM pg_settings
  WHERE name IN ('old_snapshot_threshold','db_user_namespace','trace_recovery_messages')
  ORDER BY name;
SELECT count(*) FROM pg_settings WHERE name IN ('old_snapshot_threshold','db_user_namespace','trace_recovery_messages');
SELECT count(*) FROM pg_proc WHERE proname LIKE 'pg_old_snapshot%';
