SHOW wal_level;
SHOW max_replication_slots;
SHOW max_wal_senders;
SELECT name, setting FROM pg_settings WHERE name IN
  ('wal_level','max_replication_slots','max_wal_senders') ORDER BY name;
