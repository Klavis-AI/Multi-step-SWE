SELECT pg_drop_replication_slot('q2_rsd_missing');
SELECT slot_name FROM pg_create_physical_replication_slot('q2_rsd_slot');
SELECT slot_name FROM pg_create_physical_replication_slot('q2_rsd_slot');
SELECT pg_drop_replication_slot('q2_rsd_slot');
SELECT pg_drop_replication_slot('q2_rsd_slot');
SELECT count(*) FROM pg_replication_slots WHERE slot_name = 'q2_rsd_slot';
