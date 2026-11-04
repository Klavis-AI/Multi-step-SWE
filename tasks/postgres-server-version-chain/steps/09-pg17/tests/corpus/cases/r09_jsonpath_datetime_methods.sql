SELECT jsonb_path_query('"2020-01-02"', '$.date()');
SELECT jsonb_path_query('"12:34:56"', '$.time()');
SELECT jsonb_path_query('"2020-01-02T03:04:05"', '$.timestamp()');
SELECT jsonb_path_query_array('["2020-01-02","2020-02-03"]', '$[*].date()');
