SELECT pg_input_is_valid('x','nosuchtype_r08');
SELECT pg_input_is_valid('x','int[]');
SELECT pg_input_is_valid(NULL,'int') IS NULL;
SELECT message FROM pg_input_error_info('x','nosuchtype_r08');
SELECT proname, pg_get_function_result(oid) FROM pg_proc WHERE proname IN ('pg_input_is_valid','pg_input_error_info') ORDER BY proname;
