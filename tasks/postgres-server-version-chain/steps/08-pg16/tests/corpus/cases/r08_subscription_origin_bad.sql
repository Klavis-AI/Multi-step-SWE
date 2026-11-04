CREATE SUBSCRIPTION r08_sub_origin_bad CONNECTION 'host=127.0.0.1 port=1 dbname=r08bad' PUBLICATION r08_pub_bad WITH (connect = false, origin = bogus, slot_name = NONE);
SELECT count(*) FROM pg_subscription WHERE subname = 'r08_sub_origin_bad';
CREATE SUBSCRIPTION r08_sub_origin_bad CONNECTION 'host=127.0.0.1 port=1 dbname=r08bad' PUBLICATION r08_pub_bad WITH (connect = false, origin = 'NONE', slot_name = NONE);
DROP SUBSCRIPTION IF EXISTS r08_sub_origin_bad;
