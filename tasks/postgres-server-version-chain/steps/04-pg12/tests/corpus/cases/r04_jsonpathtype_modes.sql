SELECT 'lax $.a'::jsonpath;
SELECT 'strict $.a'::jsonpath;
SELECT '$."odd key"'::jsonpath;
SELECT '$[0 to 2]'::jsonpath;
SELECT '$.a + 1'::jsonpath;
SELECT ('$.a'::jsonpath)::text;
