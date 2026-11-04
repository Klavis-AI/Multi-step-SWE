SELECT * FROM JSON_TABLE('[1,2]'::jsonb, '$[*]'
       COLUMNS (j jsonb PATH '$', t text FORMAT JSON PATH '$')) ORDER BY 1;
SELECT * FROM JSON_TABLE('[{"d":"2020-01-02","n":1.5}]'::jsonb, '$[*]'
       COLUMNS (d date PATH '$.d', n numeric(5,2) PATH '$.n')) ORDER BY d;
SELECT * FROM JSON_TABLE('[{"s":"txt"}]'::jsonb, '$[*]'
       COLUMNS (s varchar(2) PATH '$.s')) ORDER BY s;
