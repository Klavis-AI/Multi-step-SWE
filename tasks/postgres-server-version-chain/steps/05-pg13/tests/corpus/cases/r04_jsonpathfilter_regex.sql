SELECT jsonb_path_query_array('["foobar","barfoo","baz"]'::jsonb, '$[*] ? (@ like_regex "^foo")');
SELECT jsonb_path_query_array('["foobar","FOOBAR"]'::jsonb, '$[*] ? (@ like_regex "foo" flag "i")');
SELECT jsonb_path_query_array('["foobar","barfoo"]'::jsonb, '$[*] ? (@ starts with "bar")');
SELECT jsonb_path_query_array('["a","bb","ccc"]'::jsonb, '$[*] ? (@.size() >= 0)');
SELECT '["foobar"]'::jsonb @? '$[*] ? (@ like_regex "o{2}")';
