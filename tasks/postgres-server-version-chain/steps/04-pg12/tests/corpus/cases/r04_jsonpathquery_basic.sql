SELECT ord, v FROM jsonb_path_query('{"a":[1,2,3]}'::jsonb, '$.a[*]')
  WITH ORDINALITY AS t(v, ord) ORDER BY ord;
SELECT jsonb_path_query_array('{"a":[1,2,3]}'::jsonb, '$.a[*]');
SELECT jsonb_path_query_first('{"a":[1,2,3]}'::jsonb, '$.a[*]');
SELECT jsonb_path_query_first('{"a":[1,2,3]}'::jsonb, '$.b');
