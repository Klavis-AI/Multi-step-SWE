SELECT js, js IS JSON VALUE AS v, js IS JSON SCALAR AS s, js IS JSON OBJECT AS o, js IS JSON ARRAY AS a
  FROM (VALUES ('123'),('"a"'),('{"a":1}'),('[1]'),('bad'),('null')) t(js) ORDER BY js;
SELECT '[]'::text IS JSON ARRAY, '{}'::text IS JSON OBJECT;
SELECT '1'::text IS JSON SCALAR, '[1]'::text IS JSON SCALAR;
