SELECT JSON_VALUE(jsonb '{"a":"x"}', '$.a');
SELECT JSON_VALUE(jsonb '{"a":1}', '$.a' RETURNING int);
SELECT JSON_VALUE(jsonb '{"a":"2020-01-02"}', '$.a' RETURNING date);
SELECT pg_typeof(JSON_VALUE(jsonb '{"a":1}', '$.a'));
SELECT JSON_VALUE(jsonb '{"a":[1,2]}', '$.a');
