SELECT a.g, b.g FROM generate_series(1,2) a(g) JOIN generate_series(1,2) b(g) USING (g) ORDER BY 1;
