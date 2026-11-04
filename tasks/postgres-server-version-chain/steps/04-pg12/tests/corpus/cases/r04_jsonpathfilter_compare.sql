SELECT jsonb_path_query_array('[1,2,3,4]'::jsonb, '$[*] ? (@ > 2)');
SELECT jsonb_path_query_array('[1,2,3,4]'::jsonb, '$[*] ? (@ >= 2 && @ <= 3)');
SELECT jsonb_path_query_array('[1,2,3,4]'::jsonb, '$[*] ? (@ == 1 || @ == 4)');
SELECT jsonb_path_query_array('[1,2,3,4]'::jsonb, '$[*] ? (!(@ > 2))');
SELECT jsonb_path_query_array('[1,2,3,4]'::jsonb, '$[*] ? (@ != 2)');
