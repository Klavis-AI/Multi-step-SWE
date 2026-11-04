SELECT typname, typtype FROM pg_type WHERE typname IN ('int4','text','numeric','bool','jsonb','_int4') ORDER BY typname;
SELECT 'int4'::regtype, 'text'::regtype::oid, 23::regtype;
SELECT pg_typeof(1), pg_typeof(1::int8), pg_typeof('x'), pg_typeof('x'::text), pg_typeof(1.5), pg_typeof(1.5::float8);
