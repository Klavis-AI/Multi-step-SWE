SELECT to_regclass('pg_stat_slru') IS NOT NULL AS slru,
       to_regclass('pg_stat_progress_analyze') IS NOT NULL AS analyze_prog,
       to_regclass('pg_stat_progress_basebackup') IS NOT NULL AS basebackup_prog,
       to_regclass('pg_shmem_allocations') IS NOT NULL AS shmem;
SELECT count(*) FROM pg_stat_progress_analyze;
SELECT count(*) FROM pg_stat_progress_basebackup;
SELECT count(*) >= 0 FROM pg_stat_progress_cluster;
