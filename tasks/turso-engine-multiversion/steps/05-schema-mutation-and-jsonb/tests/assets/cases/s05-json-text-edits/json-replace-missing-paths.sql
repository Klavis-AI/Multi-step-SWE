-- R05.14: json_replace leaves a path that does not exist alone, which is what
-- separates it from json_set.
--
-- Each SELECT names at least one absent path -- a missing object key, an array
-- subscript past the end, a key in an empty object -- and the document comes
-- back without it. The '$.c',9,'$.a',2 call shows the present path still being
-- written while the absent one is skipped.
--
-- The two NULL calls are the two halves of the NULL rule R05.14 states, and
-- they are separate assertions because they are separate behaviours. A
-- replacement value of SQL NULL is a value like any other: on a path that
-- EXISTS it is written, and that path's value becomes JSON null; on a path
-- that does NOT exist it is still not written, so "absent" is a property of
-- the path and not of the value. The last call applies NULL to one of each in
-- a single statement, so an implementation reading NULL as "delete this path"
-- or as "create this path" answers something other than {"a":null} there.
-- Stock sqlite3 3.50.2 gives the same six answers.
SELECT json_replace('{"a":1}','$.c',9);
SELECT json_replace('{"a":1}','$.a',NULL);
SELECT json_replace('[1,2]','$[5]','x');
SELECT json_replace('{"a":1}','$.c',9,'$.a',2);
SELECT json_replace('{}','$.a',1);
SELECT json_replace('{"a":1}','$.c',NULL,'$.a',NULL);
