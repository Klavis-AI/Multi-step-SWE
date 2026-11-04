SELECT pg_create_logical_replication_slot('r08_slot_x','pgoutput');
SELECT pg_create_logical_replication_slot('r08_slot_y','pgoutput', true);
SELECT count(*) FROM pg_replication_slots WHERE slot_name LIKE 'r08_slot%';
SELECT proname, pg_get_function_arguments(oid) FROM pg_proc WHERE proname = 'pg_create_logical_replication_slot' ORDER BY 1;
