-- json1.html 2.1 "JSON arguments": "If an SQLite text value that is not a
-- well-formed JSON object, array, or string is passed into json1 function,
-- that function will usually throw an error.  (An exception is the
-- json_valid(X) function which returns 1 if X is well-formed JSON and 0 if it
-- is not.)"  Error text is never compared -- TEST_INTERFACE.md 2.3.
SELECT json_valid('nope');
SELECT json('{"ok":1}');
SELECT json('nope');
SELECT json('{"x":35');
SELECT json_type('[1,');
SELECT json_extract('{a:1}','$.a');
SELECT json_array_length('[1,');
SELECT json_remove('nope','$.a');
SELECT json_set('nope','$.a',1);
SELECT json_replace('[1,','$[0]',2);
SELECT json_insert('}{','$.a',1);
SELECT json('[1,2]');
