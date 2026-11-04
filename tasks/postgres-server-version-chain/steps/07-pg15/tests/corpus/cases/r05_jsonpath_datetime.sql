SELECT jsonb_path_query('"2020-01-02"', '$.datetime()');
SELECT jsonb_path_query('"2020-01-02"', '$.datetime("YYYY-MM-DD")');
SELECT jsonb_path_query('"12:34:56"', '$.datetime()');
SELECT jsonb_path_query('"2020-01-02T03:04:05"', '$.datetime()');
SELECT jsonb_path_query_array('["2020-01-02","2020-01-03"]', '$[*].datetime()');
