SELECT slot_name FROM pg_create_logical_replication_slot('q2_rsl_slot','test_decoding');
SELECT count(*) FROM pg_replication_slots WHERE slot_name = 'q2_rsl_slot';
SHOW wal_level;
SHOW max_replication_slots;
