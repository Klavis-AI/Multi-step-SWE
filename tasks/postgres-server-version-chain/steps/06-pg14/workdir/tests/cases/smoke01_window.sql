SELECT g, row_number() OVER (ORDER BY g) FROM generate_series(1,3) g ORDER BY g;
