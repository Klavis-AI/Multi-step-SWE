SELECT jsonb_path_query_array('{"a":[1,2,3]}'::jsonb, '$.a[*] ? (@ > $min)', '{"min":1}');
SELECT jsonb_path_query_array('{"a":[1,2,3]}'::jsonb, '$.a[*] ? (@ >= $lo && @ <= $hi)',
  '{"lo":2,"hi":3}');
SELECT jsonb_path_exists('{"a":1}'::jsonb, '$.a ? (@ == $v)', '{"v":1}');
SELECT jsonb_path_query_array('{"a":["x","y"]}'::jsonb, '$.a[*] ? (@ == $s)', '{"s":"y"}');
SELECT jsonb_path_query_array('{"a":[1]}'::jsonb, '$.a[*] ? (@ > $nosuch)');
