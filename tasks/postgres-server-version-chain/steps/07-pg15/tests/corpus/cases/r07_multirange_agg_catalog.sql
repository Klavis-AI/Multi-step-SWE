SELECT proname, proargtypes::regtype[]::text[] AS argtypes FROM pg_proc
  WHERE proname IN ('range_agg','range_intersect_agg') ORDER BY proname, pronargs, argtypes;
SELECT count(*) FROM pg_proc WHERE proname = 'range_agg';
SELECT count(*) FROM pg_proc WHERE proname = 'range_intersect_agg';
