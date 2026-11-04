SELECT attname FROM pg_attribute WHERE attrelid = 'pg_publication'::regclass AND attnum > 0 ORDER BY attname;
SELECT column_name FROM information_schema.columns WHERE table_name = 'pg_publication_tables' ORDER BY 1;
SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_publication_rel'::regclass AND attname IN ('prattrs','prqual');
