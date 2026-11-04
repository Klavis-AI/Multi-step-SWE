-- "Removals occurs sequentially from left to right.  Changes caused by prior
-- removals can affect the path search for subsequent arguments."  The two
-- examples on the page differ only in the order of the same two paths and
-- give different answers; the pair below does the same on an object, where
-- the answer does not depend on order, and then on nested containers.
SELECT json_remove('{"a":1,"b":2,"c":3}','$.a','$.c');
SELECT json_remove('{"a":{"b":1,"c":2}}','$.a.b');
SELECT json_remove('[[1,2],[3,4]]','$[0][1]');
SELECT json_remove('{"a":[10,20,30]}','$.a[1]');
-- "Paths that select elements not found in X are silently ignored."
SELECT json_remove('[1,2]','$[9]');
SELECT json_remove('{"a":{"b":1}}','$.a.zz','$.zz');
-- With no path argument the input is reformatted, whitespace removed.
SELECT json_remove('  [ 1 ,  2 ] ');
