CREATE SUBSCRIPTION r09_sub_failover CONNECTION 'host=127.0.0.1 port=1 dbname=r09fo' PUBLICATION r09_pub_fo WITH (connect = false, failover = true, slot_name = NONE);
SELECT subname, subfailover FROM pg_subscription WHERE subname = 'r09_sub_failover';
DROP SUBSCRIPTION IF EXISTS r09_sub_failover;
SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_subscription'::regclass AND attname = 'subfailover';
