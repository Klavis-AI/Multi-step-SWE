SELECT sha256(1);
SELECT sha256('abc'::text);
SELECT sha256('abc'::bytea, 'x');
SELECT pg_typeof(sha256('abc'::bytea));
