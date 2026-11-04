SELECT typname, typtype, typcategory FROM pg_type WHERE typname = 'jsonpath';
SELECT count(*) FROM pg_type WHERE typname = 'jsonpath';
SELECT oprname FROM pg_operator WHERE oprname IN ('@?','@@')
  AND oprleft = 'jsonb'::regtype ORDER BY oprname;
SELECT proname FROM pg_proc WHERE proname LIKE 'jsonb\_path\_%' ORDER BY proname;
