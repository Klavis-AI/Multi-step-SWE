SELECT jsonb_path_query_array('{"a":1,"b":2}'::jsonb, '$.keyvalue()');
SELECT jsonb_path_query_array('{"a":1,"b":2}'::jsonb, '$.keyvalue().key');
SELECT jsonb_path_query_array('{"a":1}'::jsonb, '$.keyvalue() ? (@.key == "a").value');
SELECT jsonb_path_query('[1]'::jsonb, 'strict $.keyvalue()');
