SELECT slot_name FROM pg_create_physical_replication_slot('q2_rsp_slot');
SELECT slot_name, plugin IS NULL AS no_plugin, slot_type, database IS NULL AS no_db, active
  FROM pg_replication_slots WHERE slot_name = 'q2_rsp_slot';
SELECT pg_drop_replication_slot('q2_rsp_slot');
SELECT count(*) FROM pg_replication_slots WHERE slot_name = 'q2_rsp_slot';
