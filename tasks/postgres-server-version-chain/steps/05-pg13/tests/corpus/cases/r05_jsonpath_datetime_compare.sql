SELECT jsonb_path_query('"2020-01-02"', '$.datetime() < "2021-01-01".datetime()');
SELECT jsonb_path_exists('"2020-01-02"', '$.datetime() >= "2020-01-01".datetime()');
SELECT jsonb_path_query_first('["2020-01-02","2019-01-02"]',
  '$[*].datetime() ? (@ < "2020-01-01".datetime())');
SELECT jsonb_path_query('"2020-01-02 03:04:05+02"', '$.datetime()');
