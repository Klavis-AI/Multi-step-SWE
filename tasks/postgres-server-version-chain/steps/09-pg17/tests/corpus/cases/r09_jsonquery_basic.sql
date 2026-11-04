SELECT JSON_QUERY(jsonb '{"a":[1,2]}', '$.a');
SELECT JSON_QUERY(jsonb '{"a":[1,2]}', '$.a[*]' WITH WRAPPER);
SELECT JSON_QUERY(jsonb '{"a":[1,2]}', '$.a[*]' WITH CONDITIONAL WRAPPER);
SELECT JSON_QUERY(jsonb '{"a":[1]}', '$.a[*]' WITHOUT WRAPPER);
SELECT JSON_QUERY(jsonb '{"a":1}', '$.a' RETURNING int);
