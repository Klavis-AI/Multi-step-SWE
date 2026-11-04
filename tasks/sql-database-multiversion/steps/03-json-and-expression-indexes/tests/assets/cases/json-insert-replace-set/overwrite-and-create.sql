-- json1.html 3.5: json_insert() overwrites nothing and creates what is
-- missing; json_replace() overwrites what exists and creates nothing;
-- json_set() does both.  The six lines below are that section's own worked
-- examples, one per cell of its table.
SELECT json_insert('{"a":2,"c":4}', '$.a', 99);
SELECT json_insert('{"a":2,"c":4}', '$.e', 99);
SELECT json_replace('{"a":2,"c":4}', '$.a', 99);
SELECT json_replace('{"a":2,"c":4}', '$.e', 99);
SELECT json_set('{"a":2,"c":4}', '$.a', 99);
SELECT json_set('{"a":2,"c":4}', '$.e', 99);
-- The same three functions against one existing and one missing path inside
-- an array and inside a nested object.
SELECT json_insert('{"a":[1,2]}','$.a[0]',9), json_insert('{"a":[1,2]}','$.a[2]',9);
SELECT json_replace('{"a":{"b":1}}','$.a.b',9), json_replace('{"a":{"b":1}}','$.a.z',9);
SELECT json_set('{"a":{"b":1}}','$.a.b',9), json_set('{"a":{"b":1}}','$.a.z',9);
