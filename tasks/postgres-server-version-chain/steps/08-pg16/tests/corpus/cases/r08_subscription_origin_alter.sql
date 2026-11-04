CREATE SUBSCRIPTION r08_sub_origin_alt CONNECTION 'host=127.0.0.1 port=1 dbname=r08alt' PUBLICATION r08_pub_alt WITH (connect = false, slot_name = NONE);
ALTER SUBSCRIPTION r08_sub_origin_alt SET (origin = none);
SELECT subname, suborigin FROM pg_subscription WHERE subname = 'r08_sub_origin_alt';
ALTER SUBSCRIPTION r08_sub_origin_alt SET (origin = any);
SELECT subname, suborigin FROM pg_subscription WHERE subname = 'r08_sub_origin_alt';
ALTER SUBSCRIPTION r08_sub_origin_alt SET (origin = bogus);
DROP SUBSCRIPTION IF EXISTS r08_sub_origin_alt;
