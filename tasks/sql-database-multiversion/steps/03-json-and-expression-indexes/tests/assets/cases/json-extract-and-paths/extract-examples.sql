-- json1.html 3.4 "The json_extract() function": "The json_extract(X,P1,P2,...)
-- extracts and returns one or more values from the well-formed JSON at X."
-- The seven lines below are that section's own worked examples.
SELECT json_extract('{"a":2,"c":[4,5,{"f":7}]}', '$');
SELECT json_extract('{"a":2,"c":[4,5,{"f":7}]}', '$.c');
SELECT json_extract('{"a":2,"c":[4,5,{"f":7}]}', '$.c[2]');
SELECT json_extract('{"a":2,"c":[4,5,{"f":7}]}', '$.c[2].f');
SELECT json_extract('{"a":2,"c":[4,5],"f":7}','$.c','$.a');
SELECT json_extract('{"a":2,"c":[4,5,{"f":7}]}', '$.x');
SELECT json_extract('{"a":2,"c":[4,5,{"f":7}]}', '$.x', '$.a');
