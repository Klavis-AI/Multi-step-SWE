SELECT JSON(' {"a":1} ');
SELECT JSON('{"a":1,"a":2}');
SELECT JSON('{"a":1,"a":2}' WITH UNIQUE KEYS);
SELECT JSON('{"a":1}' WITHOUT UNIQUE KEYS);
SELECT pg_typeof(JSON('1'));
