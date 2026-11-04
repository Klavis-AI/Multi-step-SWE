SELECT length(uuidv4()::text);
SELECT uuid_extract_version(uuidv4());
SELECT substring(uuidv4()::text, 15, 1);
SELECT substring(uuidv4()::text, 9, 1), substring(uuidv4()::text, 14, 1);
SELECT count(DISTINCT uuid_extract_version(uuidv4())) FROM generate_series(1,20);
