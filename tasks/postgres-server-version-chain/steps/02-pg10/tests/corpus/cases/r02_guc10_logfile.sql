SELECT pg_current_logfile() IS NULL OR pg_current_logfile() <> '' AS logfile_callable;
SELECT to_regclass('pg_catalog.pg_hba_file_rules') IS NOT NULL AS has_hba_view;
SELECT to_regclass('pg_catalog.pg_sequence') IS NOT NULL AS has_pg_sequence;
SELECT to_regclass('pg_catalog.pg_statistic_ext') IS NOT NULL AS has_stat_ext;
SELECT to_regclass('pg_catalog.pg_partitioned_table') IS NOT NULL AS has_part_table;
SELECT count(*) FROM pg_proc WHERE proname = 'pg_current_logfile';
