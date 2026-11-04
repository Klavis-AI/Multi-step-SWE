SELECT 0.10::numeric, 1.00::numeric+2.000::numeric, 2*3.50::numeric;
SELECT 1e-10::numeric, 1e10::numeric, (10^30)::numeric;
SELECT 123456789012345678901234567890::numeric * 987654321;
SELECT 'NaN'::numeric, 'NaN'::numeric + 1, 'NaN'::numeric > 1e100::numeric;
SELECT scale(1.230::numeric), scale(5::numeric);
