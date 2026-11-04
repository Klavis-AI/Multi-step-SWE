SELECT jsonb_path_query_tz('"2020-01-02"', '$.datetime()');
SELECT jsonb_path_exists_tz('"2020-01-02"', '$.datetime()');
SELECT jsonb_path_match_tz('"2020-01-02"', '$.datetime() == $.datetime()');
SELECT jsonb_path_query_first_tz('["2020-01-02"]', '$[*].datetime()');
SELECT jsonb_path_query_array_tz('["2020-01-02"]', '$[*].datetime()');
