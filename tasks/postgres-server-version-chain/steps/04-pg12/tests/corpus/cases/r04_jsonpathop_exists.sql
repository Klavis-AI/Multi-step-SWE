SELECT '{"a":[1,2,3]}'::jsonb @? '$.a';
SELECT '{"a":[1,2,3]}'::jsonb @? '$.b';
SELECT '{"a":[1,2,3]}'::jsonb @? '$.a[*] ? (@ > 2)';
SELECT '{"a":[1,2,3]}'::jsonb @? '$.a[*] ? (@ > 9)';
SELECT '[1,2]'::jsonb @? '$[*] ? (@ == 2)';
