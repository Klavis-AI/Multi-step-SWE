SELECT to_bin(42), to_oct(42), to_hex(42);
SELECT to_bin(0), to_oct(0), to_bin(1);
SELECT to_bin(255::int8), to_oct(255::int8), to_hex(255::int8);
SELECT to_bin((-1)::int4), to_oct((-1)::int4);
