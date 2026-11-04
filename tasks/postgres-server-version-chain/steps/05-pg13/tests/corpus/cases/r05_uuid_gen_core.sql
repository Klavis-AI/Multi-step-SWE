SELECT length(gen_random_uuid()::text);
SELECT pg_typeof(gen_random_uuid());
SELECT gen_random_uuid() IS NOT NULL;
SELECT gen_random_uuid() = gen_random_uuid();
