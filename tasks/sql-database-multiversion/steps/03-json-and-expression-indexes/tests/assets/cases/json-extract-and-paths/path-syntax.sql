-- json1.html 2.2 "PATH arguments": "A well-formed PATH is a text value that
-- begins with exactly one '$' character followed by zero or more instances of
-- '.objectlabel' or '[arrayindex]'."
SELECT json_extract('{"a":1}','$');
SELECT json_extract('{"a":1}','$.a');
SELECT json_extract('[10,20,30]','$[0]');
SELECT json_extract('[10,20,30]','$[2]');
SELECT json_extract('[[1,2],[3,4]]','$[1][0]');
SELECT json_extract('{"a":{"b":{"c":[10,20]}}}','$.a.b.c[1]');
SELECT json_extract('{"a":[{"b":1},{"b":2}]}','$.a[1].b');
-- An index past the end of an array, and a label that is not present, select
-- no element; 3.4 says the result is then NULL.
SELECT json_extract('[10,20]','$[5]');
SELECT json_extract('{"a":1}','$.zz');
SELECT json_extract('{"a":{"b":1}}','$.a.zz');
