SELECT uuid_extract_version(gen_random_uuid());
SELECT length(gen_random_uuid()::text);
SELECT substring(gen_random_uuid()::text, 15, 1);
SELECT count(DISTINCT uuid_extract_version(gen_random_uuid())) FROM generate_series(1,10);
