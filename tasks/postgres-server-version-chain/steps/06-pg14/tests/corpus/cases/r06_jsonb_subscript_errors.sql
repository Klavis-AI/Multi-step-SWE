SELECT ('x'::text)['a'];
SELECT (1::int)['a'];
SELECT ('{"a":1}'::json)['a'];
CREATE TABLE x06_28a(id int, j jsonb);
INSERT INTO x06_28a VALUES (1,'[1,2,3]');
UPDATE x06_28a SET j['a'] = '1';
UPDATE x06_28a SET j[0] = 'notjson';
DROP TABLE x06_28a;
