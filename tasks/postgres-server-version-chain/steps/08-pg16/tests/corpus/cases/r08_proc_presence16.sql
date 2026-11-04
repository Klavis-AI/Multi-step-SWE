SELECT proname FROM pg_proc
  WHERE proname IN ('any_value','array_sample','array_shuffle','random_normal',
                    'pg_input_is_valid','pg_input_error_info','pg_split_walfile_name',
                    'erf','erfc','date_add','date_subtract','pg_log_standby_snapshot',
                    'system_user','xmltext','to_bin')
  GROUP BY proname ORDER BY proname;
SELECT count(DISTINCT proname) FROM pg_proc WHERE proname LIKE 'json%agg%strict';
SELECT count(DISTINCT proname) FROM pg_proc WHERE proname LIKE '%object_agg_unique%';
