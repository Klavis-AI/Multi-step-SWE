SELECT * FROM JSON_TABLE('{"id":1,"tags":["p","q"]}'::jsonb, '$'
       COLUMNS (id int PATH '$.id',
                NESTED PATH '$.tags[*]' COLUMNS (o FOR ORDINALITY, tag text PATH '$')))
  ORDER BY id, o;
SELECT * FROM JSON_TABLE('{"a":[1,2],"b":["x"]}'::jsonb, '$'
       COLUMNS (NESTED PATH '$.a[*]' COLUMNS (av int PATH '$'),
                NESTED PATH '$.b[*]' COLUMNS (bv text PATH '$')))
  ORDER BY av NULLS LAST, bv NULLS LAST;
