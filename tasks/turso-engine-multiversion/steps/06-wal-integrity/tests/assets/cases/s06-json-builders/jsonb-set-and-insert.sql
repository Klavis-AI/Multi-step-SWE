-- R06.15: jsonb_set overwrites an existing path where json_insert leaves it
-- alone.
--
-- jsonb_set overwrites or creates; json_insert only creates. The difference is
-- what an existing path does.
SELECT json(jsonb_set('{"a":1}','$.b',3));
SELECT json(jsonb_set('{"a":1}','$.a',3));
SELECT json_insert('{"a":1}','$.b',2);
SELECT json_insert('{"a":1}','$.a',2);
SELECT json(jsonb_set(jsonb('{"a":{"b":1}}'),'$.a.b',json('[1]')));
SELECT json_type(jsonb_set('{"a":1}','$.b','s'),'$.b');
