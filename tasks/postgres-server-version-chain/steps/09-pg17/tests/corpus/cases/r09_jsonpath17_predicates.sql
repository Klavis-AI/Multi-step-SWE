SELECT jsonb_path_query_array('["1","20","3"]', '$[*].integer() ? (@ > 2)');
SELECT jsonb_path_exists('{"d":"2020-05-06"}', '$.d.date() ? (@ > "2020-01-01".date())');
SELECT jsonb_path_match('{"n":"5"}', '$.n.integer() > 3');
SELECT jsonb_path_query_first('["a",1]', '$[*].string()');
