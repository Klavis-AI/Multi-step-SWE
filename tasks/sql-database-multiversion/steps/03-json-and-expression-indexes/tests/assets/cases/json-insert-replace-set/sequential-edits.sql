-- json1.html 3.5: "The json_insert(), json_replace(), and json_set()
-- functions always take an odd number of arguments.  The first argument is
-- always the original JSON to be edited.  Subsequent arguments occur in pairs
-- with the first element of each pair being a path and the second element
-- being an value to insert or replace or set on that path."
SELECT json_insert('{"a":1}','$.b',2,'$.c',3);
SELECT json_set('{"a":1}','$.a',2,'$.b',3);
SELECT json_replace('{"a":1,"b":2}','$.a',9,'$.c',9);
-- "Edits occurs sequentially from left to right.  Changes caused by prior
-- edits can affect the path search for subsequent edits."  Below: the second
-- pair overwrites what the first pair set; the second json_insert() pair
-- finds the member the first pair created and therefore leaves it alone; and
-- three appends to an array each see the element the previous one added.
-- PROFILE.md 3.7 keeps the harder form of this rule out of scope.
SELECT json_set('{"a":1}','$.a',2,'$.a',3);
SELECT json_set('{"a":1}','$.b','x','$.b',7);
SELECT json_insert('{"a":1}','$.b',2,'$.b',3);
SELECT json_insert('[1]','$[1]',2,'$[2]',3);
-- Zero pairs is the odd argument count with no edit at all.
SELECT json_insert('{"a":1}'), json_replace('{"a":1}'), json_set('{"a":1}');
