SELECT ('{"a":1}'::jsonb @? '$.a') = jsonb_path_exists('{"a":1}'::jsonb, '$.a') AS same_exists;
SELECT ('{"a":1}'::jsonb @@ '$.a == 1') = jsonb_path_match('{"a":1}'::jsonb, '$.a == 1') AS same_match;
SELECT jsonb_path_exists('{"a":1}'::jsonb, '$.b');
SELECT jsonb_path_match('{"a":1}'::jsonb, '$.b == 1');
SELECT jsonb_path_match('{"a":[1,2]}'::jsonb, '$.a[*] == 1');
