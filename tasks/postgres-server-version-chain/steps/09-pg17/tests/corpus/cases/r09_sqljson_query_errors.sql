SELECT JSON_VALUE(jsonb '{"a":1}', '$.a' RETURNING nosuchtype_r09);
SELECT JSON_QUERY(jsonb '{"a":[1,2]}', '$.a[*]');
SELECT JSON_EXISTS(jsonb '{"a":1}', '$$bad');
SELECT JSON_VALUE(jsonb '{"a":1}', '$.a' OMIT QUOTES);
SELECT JSON_QUERY(jsonb '{"a":"x"}', '$.a' RETURNING int OMIT QUOTES ERROR ON ERROR);
