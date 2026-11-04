SELECT proname, pronargs FROM pg_proc
  WHERE proname IN ('regexp_count','regexp_instr','regexp_like','regexp_substr')
  ORDER BY proname, pronargs;
SELECT count(*) FROM pg_proc WHERE proname = 'regexp_replace';
SELECT proname, prorettype::regtype::text FROM pg_proc
  WHERE proname IN ('regexp_count','regexp_like') ORDER BY proname, pronargs LIMIT 2;
