SELECT jsonb_path_query('"notadate"', '$.datetime()');
SELECT jsonb_path_query('"2020-13-45"', '$.datetime()');
SELECT jsonb_path_query('1', '$.datetime()');
SELECT jsonb_path_query('"2020-01-02"', '$.datetime("NOSUCH")');
SELECT jsonb_path_query('"2020-01-02"', '$.nosuchmethod()');
