SHOW sync_replication_slots;
SHOW synchronized_standby_slots;
SELECT name, vartype, boot_val FROM pg_settings WHERE name = 'sync_replication_slots';
SELECT count(*) FROM pg_settings WHERE name IN ('standby_slot_names','synchronized_standby_slots');
