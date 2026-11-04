SELECT proname, proretset, pg_get_function_arguments(oid) FROM pg_proc WHERE proname = 'pg_split_walfile_name' ORDER BY 1;
SELECT count(*) FROM pg_proc WHERE proname IN ('pg_split_walfile_name','pg_walfile_name','pg_walfile_name_offset');
SELECT proname FROM pg_proc WHERE proname LIKE 'pg_%walfile%' ORDER BY proname;
