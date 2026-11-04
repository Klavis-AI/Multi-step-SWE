-- json1.html 3.4: "If only a single path P1 is provided, then the SQL
-- datatype of the result is NULL for a JSON null, INTEGER or REAL for a JSON
-- numeric value, an INTEGER zero for a JSON false value, an INTEGER one for a
-- JSON true value, the dequoted text for a JSON string value, and a text
-- representation for JSON object and array values."
SELECT typeof(json_extract('{"a":null}','$.a')), json_extract('{"a":null}','$.a');
SELECT typeof(json_extract('{"a":2}','$.a')), json_extract('{"a":2}','$.a');
SELECT typeof(json_extract('{"a":2.5}','$.a')), json_extract('{"a":2.5}','$.a');
SELECT typeof(json_extract('{"a":false}','$.a')), json_extract('{"a":false}','$.a');
SELECT typeof(json_extract('{"a":true}','$.a')), json_extract('{"a":true}','$.a');
SELECT typeof(json_extract('{"a":"xy"}','$.a')), json_extract('{"a":"xy"}','$.a');
SELECT typeof(json_extract('{"a":[1,2]}','$.a')), json_extract('{"a":[1,2]}','$.a');
SELECT typeof(json_extract('{"a":{"b":1}}','$.a')), json_extract('{"a":{"b":1}}','$.a');
-- "the dequoted text for a JSON string value"
SELECT json_extract('{"a":"he said \"hi\""}','$.a');
-- "If there are multiple path arguments (P1, P2, and so forth) then this
-- routine returns SQLite text which is a well-formed JSON array holding the
-- various values."
SELECT typeof(json_extract('{"a":1,"b":2}','$.a','$.b')), json_extract('{"a":1,"b":2}','$.a','$.b');
SELECT json_valid(json_extract('{"a":1,"b":"z"}','$.a','$.b'));
