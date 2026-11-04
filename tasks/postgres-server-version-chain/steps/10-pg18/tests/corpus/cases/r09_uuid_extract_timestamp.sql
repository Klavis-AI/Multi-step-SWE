SELECT uuid_extract_timestamp('c232ab00-9414-11ec-b3c8-9f6bdeced846'::uuid);
SELECT uuid_extract_timestamp('018f5a3c-0000-7000-8000-000000000000'::uuid);
SELECT uuid_extract_timestamp('11111111-2222-4333-8444-555555555555'::uuid);
SELECT pg_typeof(uuid_extract_timestamp('c232ab00-9414-11ec-b3c8-9f6bdeced846'::uuid));
