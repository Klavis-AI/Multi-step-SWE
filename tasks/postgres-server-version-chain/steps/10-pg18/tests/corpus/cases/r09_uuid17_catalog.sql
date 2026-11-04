SELECT proname, pg_get_function_arguments(oid), pg_get_function_result(oid) FROM pg_proc
  WHERE proname LIKE 'uuid_extract%' ORDER BY proname;
SELECT count(*) FROM pg_proc WHERE proname IN ('uuidv4','uuidv7');
SELECT count(*) FROM pg_proc WHERE proname = 'gen_random_uuid';
