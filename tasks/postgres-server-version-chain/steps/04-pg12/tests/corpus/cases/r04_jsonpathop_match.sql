SELECT '{"a":1}'::jsonb @@ '$.a == 1';
SELECT '{"a":1}'::jsonb @@ '$.a == 2';
SELECT '{"a":1}'::jsonb @@ '$.b == 1';
SELECT '{"a":[1,2]}'::jsonb @@ '$.a[*] > 1';
SELECT '{"a":[1,2]}'::jsonb @@ 'exists($.a)';
