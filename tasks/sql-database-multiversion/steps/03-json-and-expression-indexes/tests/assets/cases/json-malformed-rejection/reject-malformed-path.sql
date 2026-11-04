-- json1.html 2.2 "PATH arguments": "For functions that accept PATH arguments,
-- that PATH must be well-formed or else the function will throw an error.  A
-- well-formed PATH is a text value that begins with exactly one '$' character
-- followed by zero or more instances of '.objectlabel' or '[arrayindex]'."
SELECT json_extract('{"a":1}','$.a');
SELECT json_extract('{"a":1}','a');
SELECT json_type('{"a":1}','a');
SELECT json_array_length('[1]','x');
SELECT json_remove('[1,2]','x');
SELECT json_insert('{}','x',1);
SELECT json_replace('{"a":1}','a',2);
SELECT json_set('{}','',9);
SELECT json_extract('{"a":1}','$');
