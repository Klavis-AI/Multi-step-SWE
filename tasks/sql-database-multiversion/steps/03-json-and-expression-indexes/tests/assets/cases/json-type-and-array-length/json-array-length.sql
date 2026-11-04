-- json1.html 3.3 "The json_array_length() function": "returns the number of
-- elements in the JSON array X, or 0 if X is some kind of JSON value other
-- than an array.  The json_array_length(X,P) locates the array at path P
-- within X and returns the length of that array, or 0 if path P locates a
-- element or X other than an JSON array, and NULL if path P does not locate
-- any element of X."  The six lines below are that section's own worked
-- examples.
SELECT json_array_length('[1,2,3,4]');
SELECT json_array_length('[1,2,3,4]', '$');
SELECT json_array_length('[1,2,3,4]', '$[2]');
SELECT json_array_length('{"one":[1,2,3]}');
SELECT json_array_length('{"one":[1,2,3]}', '$.one');
SELECT json_array_length('{"one":[1,2,3]}', '$.two');
-- The empty array, a nested array, and an array reached through a deeper path.
SELECT json_array_length('[]');
SELECT json_array_length('[[1,2],[3]]'), json_array_length('[[1,2],[3]]','$[0]');
SELECT json_array_length('{"a":{"b":[1,2,3]}}','$.a.b');
-- "0 if X is some kind of JSON value other than an array"
SELECT json_array_length('12'), json_array_length('"s"'), json_array_length('true');
SELECT typeof(json_array_length('[]')), typeof(json_array_length('{}','$.no'));
