-- json1.html 3.5: "If the value of a path/value pair is an SQLite TEXT value,
-- then it is normally inserted as a quoted JSON string, even if the string
-- looks like valid JSON.  However, if the value is the result of another json1
-- function (such as json() or json_array() or json_object()) then it is
-- interpreted as JSON and is inserted as JSON retaining all of its
-- substructure."  The three lines below are that section's own worked examples.
SELECT json_set('{"a":2,"c":4}', '$.c', '[97,96]');
SELECT json_set('{"a":2,"c":4}', '$.c', json('[97,96]'));
SELECT json_set('{"a":2,"c":4}', '$.c', json_array(97,96));
-- The same rule for json_insert() and json_replace().
SELECT json_insert('{"a":1}','$.b','{"x":1}');
SELECT json_insert('{"a":1}','$.b',json_object('x',1));
SELECT json_replace('{"a":1}','$.a','[1]');
SELECT json_replace('{"a":1}','$.a',json('[1,{"x":2}]'));
-- Non-text values keep their JSON type.
SELECT json_set('{}','$.i',7,'$.r',2.5);
