SELECT sha256(''::bytea);
SELECT sha256('abc'::bytea) = sha256('abc');
SELECT length(sha224('x')), length(sha256('x')), length(sha384('x')), length(sha512('x'));
SELECT encode(sha256('abc'), 'hex');
SELECT sha256(NULL) IS NULL;
