SELECT jsonb_path_query('"true"', '$.boolean()'), jsonb_path_query('1', '$.boolean()');
SELECT jsonb_path_query('123', '$.string()'), jsonb_path_query('true', '$.string()');
SELECT jsonb_path_query('"2020-01-02"', '$.date().string()');
SELECT jsonb_path_query('"abc"', '$.boolean()');
