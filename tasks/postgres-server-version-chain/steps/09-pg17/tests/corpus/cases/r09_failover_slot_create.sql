SELECT pg_create_logical_replication_slot('r09_fslot_a','pgoutput', failover := true);
SELECT pg_create_logical_replication_slot('r09_fslot_b','pgoutput', false, false, true);
SELECT count(*) FROM pg_replication_slots WHERE slot_name LIKE 'r09_fslot%';
