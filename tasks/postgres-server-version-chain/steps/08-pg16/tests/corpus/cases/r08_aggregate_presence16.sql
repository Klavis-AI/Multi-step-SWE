SELECT p.proname FROM pg_aggregate a JOIN pg_proc p ON p.oid = a.aggfnoid
  WHERE p.proname IN ('any_value','json_agg_strict','jsonb_agg_strict',
                      'json_object_agg_unique','jsonb_object_agg_unique')
  ORDER BY p.proname;
SELECT a.aggkind FROM pg_aggregate a JOIN pg_proc p ON p.oid = a.aggfnoid WHERE p.proname = 'any_value';
SELECT count(*) FROM pg_proc WHERE proname = 'pg_stat_get_backend_subxact';
