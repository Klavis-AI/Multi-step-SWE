SELECT uuid_extract_version(uuidv7(interval '0'));
SELECT length(uuidv7(interval '1 hour')::text);
SELECT uuid_extract_timestamp(uuidv7(interval '0')) IS NOT NULL;
SELECT uuid_extract_timestamp(uuidv7(interval '1 hour')) > uuid_extract_timestamp(uuidv7(interval '0'));
