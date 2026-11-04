SELECT name FROM pg_settings
  WHERE name IN ('allow_alter_system','io_combine_limit','transaction_timeout',
                 'summarize_wal','sync_replication_slots','synchronized_standby_slots',
                 'enable_group_by_reordering','event_triggers')
  ORDER BY name;
SHOW transaction_timeout;
SHOW allow_alter_system;
SHOW summarize_wal;
