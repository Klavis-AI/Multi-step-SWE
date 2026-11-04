SELECT * FROM JSON_TABLE('{"k":[{"n":[1,2]},{"n":[3]}]}'::jsonb, '$'
       COLUMNS (NESTED PATH '$.k[*]' COLUMNS (o1 FOR ORDINALITY,
                NESTED PATH '$.n[*]' COLUMNS (o2 FOR ORDINALITY, v int PATH '$'))))
  ORDER BY o1, o2;
SELECT count(*) FROM JSON_TABLE('{"k":[]}'::jsonb, '$'
       COLUMNS (NESTED PATH '$.k[*]' COLUMNS (v int PATH '$')));
