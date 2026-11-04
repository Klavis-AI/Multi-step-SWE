CREATE SUBSCRIPTION r08_sub_origin_none CONNECTION 'host=127.0.0.1 port=1 dbname=r08none' PUBLICATION r08_pub_none WITH (connect = false, origin = none, slot_name = NONE);
SELECT subname, subenabled, suborigin FROM pg_subscription WHERE subname = 'r08_sub_origin_none';
DROP SUBSCRIPTION IF EXISTS r08_sub_origin_none;
SELECT count(*) FROM pg_subscription WHERE subname = 'r08_sub_origin_none';
