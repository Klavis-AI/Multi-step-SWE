SELECT amname, amtype FROM pg_am WHERE amtype = 't' ORDER BY amname;
SELECT count(*) FROM pg_am WHERE amname = 'heap';
SELECT amname FROM pg_am WHERE amname IN ('btree','hash','gist','gin','spgist','brin')
  ORDER BY amname;
