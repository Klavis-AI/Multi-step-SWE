-- R03.15: json_set replaces the value at a path that exists and creates one
-- at a path that does not -- top level and nested, in an object and in an
-- array -- and json_quote renders a SQL value as the JSON text denoting it,
-- each answering exactly as stock sqlite3 answers.
-- The JSON mutator that arrives with this checkpoint. json_set replaces the
-- value at a path that exists and creates one at a path that does not, at the
-- top level and nested, in an object and in an array. json_quote turns a SQL
-- value into the JSON text that denotes it. Every answer is the text stock
-- sqlite3 returns for the same call.
SELECT json_set('{"a":1}', '$.b', 2);
SELECT json_set('{"a":1}', '$.a', 9);
SELECT json_set('{"a":{"b":1}}', '$.a.b', 'text');
SELECT json_set('{"a":{"b":1}}', '$.a.c', 'added');
SELECT json_set('[1,2,3]', '$[1]', 99);
SELECT json_set('{"a":1}', '$.b', json('[1,2]'));
SELECT json_set('{"a":1}', '$.a', NULL);
SELECT json_quote('text'), json_quote(3.5), json_quote(7);
SELECT json_type(json_set('{"a":1}', '$.b', json('[1,2]')), '$.b');
SELECT json_valid(json_set('{"a":1}', '$.b', 2)), length(json_set('{"a":1}', '$.b', 2));
