SELECT JSON_VALUE(jsonb '{"a":"zz"}', '$.a' RETURNING int DEFAULT -1 ON ERROR);
SELECT JSON_VALUE(jsonb '{"a":"zz"}', '$.a' RETURNING int ERROR ON ERROR);
SELECT JSON_VALUE(jsonb '{"a":1}', '$.b' DEFAULT 'none' ON EMPTY);
SELECT JSON_VALUE(jsonb '{"a":1}', '$.b' ERROR ON EMPTY);
SELECT JSON_VALUE(jsonb '{"a":1}', '$.b');
