SELECT jsonb_path_query_array('[{"a":1,"b":9},{"a":2,"b":8}]'::jsonb, '$[*] ? (@.a > 1)');
SELECT jsonb_path_query_array('[{"a":1,"b":9},{"a":2,"b":8}]'::jsonb, '$[*] ? (@.a > 1).b');
SELECT jsonb_path_query_array('[{"a":1},{"b":2}]'::jsonb, '$[*] ? (exists(@.a))');
SELECT jsonb_path_query_array('{"a":[1,2,3]}'::jsonb, '$.a[*] ? (@ > 1) ? (@ < 3)');
SELECT jsonb_path_query_array('[1,"x",true,null]'::jsonb, '$[*] ? (@.type() == "number")');
