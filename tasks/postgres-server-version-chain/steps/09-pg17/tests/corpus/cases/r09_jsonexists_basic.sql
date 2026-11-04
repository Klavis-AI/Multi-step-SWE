SELECT JSON_EXISTS(jsonb '{"a":1}', '$.a'), JSON_EXISTS(jsonb '{"a":1}', '$.b');
SELECT JSON_EXISTS(jsonb '[1,2]', '$[*] ? (@ > 1)'), JSON_EXISTS(jsonb '[1,2]', '$[*] ? (@ > 9)');
SELECT JSON_EXISTS(jsonb '{"a":1}', '$.a' PASSING 1 AS x);
SELECT JSON_EXISTS(jsonb '1', 'strict $.a' FALSE ON ERROR);
SELECT JSON_EXISTS(jsonb '1', 'strict $.a' ERROR ON ERROR);
