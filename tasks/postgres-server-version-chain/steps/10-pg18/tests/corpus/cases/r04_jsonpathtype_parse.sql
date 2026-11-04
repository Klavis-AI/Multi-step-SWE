SELECT '$'::jsonpath;
SELECT '$.a'::jsonpath;
SELECT '$.a.b[1]'::jsonpath;
SELECT '$[*]'::jsonpath;
SELECT '$.a[*] ? (@ > 1)'::jsonpath;
SELECT '$.**.b'::jsonpath;
SELECT pg_typeof('$'::jsonpath);
