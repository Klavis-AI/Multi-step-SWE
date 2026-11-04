SELECT jsonb_set_lax('{"a":1}','{b}','2'::jsonb,true);
SELECT jsonb_set_lax('{"a":1}','{b}','2'::jsonb,false);
SELECT jsonb_set_lax('{"a":1}','{b}',NULL,false,'use_json_null');
SELECT jsonb_set_lax('[1,2,3]','{1}','9'::jsonb);
SELECT jsonb_set_lax(NULL,'{a}','1'::jsonb) IS NULL;
