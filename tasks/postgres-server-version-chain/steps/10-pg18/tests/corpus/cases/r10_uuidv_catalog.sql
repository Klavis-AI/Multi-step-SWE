SELECT proname, pg_get_function_arguments(oid) AS args, pg_get_function_result(oid) AS res
  FROM pg_proc WHERE proname IN ('uuidv4','uuidv7','gen_random_uuid') ORDER BY proname, args;
SELECT provolatile FROM pg_proc WHERE proname = 'uuidv4';
SELECT count(*) FROM pg_proc WHERE proname LIKE 'uuid%' AND pronamespace = 'pg_catalog'::regnamespace;
