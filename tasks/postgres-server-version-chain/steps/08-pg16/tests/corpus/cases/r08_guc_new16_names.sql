SELECT name FROM pg_settings
  WHERE name IN ('createrole_self_grant','scram_iterations','reserved_connections',
                 'vacuum_buffer_usage_limit','debug_io_direct','gss_accept_delegation',
                 'max_parallel_apply_workers_per_subscription','icu_validation_level')
  ORDER BY name;
SHOW scram_iterations;
SHOW reserved_connections;
SHOW createrole_self_grant;
