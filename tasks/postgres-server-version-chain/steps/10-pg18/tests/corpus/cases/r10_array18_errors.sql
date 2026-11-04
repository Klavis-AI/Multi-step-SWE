SELECT array_sort(ARRAY[ROW(1),ROW(2)]);
SELECT array_reverse(1);
SELECT array_sort(ARRAY[1,2], NULL) IS NULL;
SELECT proname, pg_get_function_arguments(oid) AS args FROM pg_proc
  WHERE proname IN ('array_sort','array_reverse') ORDER BY proname, args;
