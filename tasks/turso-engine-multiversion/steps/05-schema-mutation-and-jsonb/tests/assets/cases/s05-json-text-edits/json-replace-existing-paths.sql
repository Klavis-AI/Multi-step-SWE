-- R05.14: json_replace overwrites a path that already exists, several in one
-- call, and accepts a whole document as the replacement value.
--
-- Each SELECT names a path that is present, so every one of them returns the
-- document with that path's value changed and nothing else changed.
SELECT json_replace('{"a":1,"b":2}','$.a',9);
SELECT json_replace('{"a":1,"b":2}','$.a',10,'$.b',20);
SELECT json_replace('[1,2,3]','$[0]','x');
SELECT json_replace('{"a":{"b":1}}','$.a.b',json('[7]'));
