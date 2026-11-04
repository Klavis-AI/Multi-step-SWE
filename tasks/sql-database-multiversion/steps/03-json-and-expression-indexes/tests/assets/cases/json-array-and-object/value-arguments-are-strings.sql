-- json1.html 2.3 "VALUE arguments": a text value argument is "interpreted as
-- a literal string and added to the result as a quoted string ... Even if the
-- input value strings look like well-formed JSON".  "However, if a value
-- argument come directly from the result of another json1 function, then the
-- argument is understood to be actual JSON and the complete JSON is inserted
-- rather than a quoted string."  3.2 adds: "The json() function can also be
-- used to force strings to be recognized as JSON."
SELECT json_array('{"a":1}');
SELECT json_array(json('{"a":1}'));
SELECT json_array(json_object('a',1));
SELECT json_object('k','[1,2]');
SELECT json_object('k',json('[1,2]'));
SELECT json_object('k',json_array(1,2));
-- "This allows calls to json_array() and json_object() to be nested." (3.2, 3.6)
SELECT json_array(json_array(json_array(1)));
SELECT json_object('a',json_object('b',json_object('c',1)));
SELECT json_array(json_object('a',1),json('[1]'),'x',2,2.5,null);
-- A json1 result is itself well-formed JSON, so 2.1 lets it be a json argument.
SELECT json_extract(json_object('a',json_array(10,20)),'$.a[1]');
