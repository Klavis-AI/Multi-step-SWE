SELECT length(uuidv7()::text);
SELECT uuid_extract_version(uuidv7());
SELECT substring(uuidv7()::text, 15, 1);
SELECT count(*) FROM (SELECT uuidv7() AS u FROM generate_series(1,20)) s WHERE uuid_extract_version(u) <> 7;
SELECT count(DISTINCT u) FROM (SELECT uuidv7() AS u FROM generate_series(1,20)) s;
