SELECT substring(gen_random_uuid()::text from 15 for 1) AS version_nibble;
SELECT substring(gen_random_uuid()::text from 20 for 1) IN ('8','9','a','b') AS variant_ok;
SELECT count(DISTINCT u) FROM (SELECT gen_random_uuid() u FROM generate_series(1,32)) s;
SELECT length(replace(gen_random_uuid()::text,'-',''));
