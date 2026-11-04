SELECT proname, pronargs, provolatile, proparallel, prorettype::regtype::text
  FROM pg_proc WHERE proname='gen_random_uuid' ORDER BY pronargs;
SELECT count(*) FROM pg_proc WHERE proname='gen_random_uuid';
SELECT n.nspname FROM pg_proc p JOIN pg_namespace n ON n.oid=p.pronamespace
  WHERE p.proname='gen_random_uuid' ORDER BY 1;
