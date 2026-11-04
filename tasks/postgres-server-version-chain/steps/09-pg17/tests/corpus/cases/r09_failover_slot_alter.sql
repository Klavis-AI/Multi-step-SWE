SELECT pg_sync_replication_slots();
CREATE SUBSCRIPTION r09_sub_fo_alt CONNECTION 'host=127.0.0.1 port=1 dbname=r09foa' PUBLICATION r09_pub_foa WITH (connect = false, slot_name = NONE);
ALTER SUBSCRIPTION r09_sub_fo_alt SET (failover = true);
SELECT subname, subfailover FROM pg_subscription WHERE subname = 'r09_sub_fo_alt';
ALTER SUBSCRIPTION r09_sub_fo_alt SET (failover = bogus);
DROP SUBSCRIPTION IF EXISTS r09_sub_fo_alt;
