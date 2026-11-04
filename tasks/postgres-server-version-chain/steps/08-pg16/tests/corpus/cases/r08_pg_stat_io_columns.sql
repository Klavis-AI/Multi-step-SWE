SELECT count(*) FROM information_schema.columns WHERE table_name = 'pg_stat_io';
SELECT column_name FROM information_schema.columns
  WHERE table_name = 'pg_stat_io' AND column_name IN ('backend_type','object','context','reads','writes','extends','hits')
  ORDER BY column_name;
SELECT count(*) FROM pg_class WHERE relname = 'pg_stat_io';
SELECT count(*) FROM pg_proc WHERE proname = 'pg_stat_get_io';
