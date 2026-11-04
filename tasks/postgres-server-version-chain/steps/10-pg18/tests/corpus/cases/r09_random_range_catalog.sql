SELECT proname, pg_get_function_arguments(oid) AS args FROM pg_proc
  WHERE proname = 'random' ORDER BY args;
SELECT count(*) FROM pg_proc WHERE proname IN ('random','random_normal');
SELECT count(*) FROM pg_proc WHERE proname = 'setseed';
