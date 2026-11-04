SELECT name, vartype, boot_val FROM pg_settings
  WHERE name IN ('compute_query_id','idle_session_timeout','default_toast_compression')
  ORDER BY name;
SELECT count(*) FROM pg_settings WHERE name IN
  ('operator_precedence_warning','vacuum_failsafe_age','client_connection_check_interval');
SELECT count(*) FROM pg_settings WHERE name LIKE 'idle\_%';
