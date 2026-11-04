SELECT proname, pronargs, prorettype::regtype::text FROM pg_proc
  WHERE proname IN ('factorial','numeric_fac') ORDER BY proname, pronargs;
SELECT count(*) FROM pg_operator WHERE oprname = '!';
SELECT count(*) FROM pg_operator WHERE oprname = '!!';
SELECT oprname FROM pg_operator WHERE oprname IN ('!','!!','@','~')
  AND oprleft = 'int8'::regtype ORDER BY oprname;
