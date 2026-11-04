SELECT min_scale(1.2300::numeric), min_scale(1.0::numeric), min_scale(0.00::numeric);
SELECT trim_scale(1.2300::numeric), trim_scale(0.00::numeric), trim_scale(100.0::numeric);
SELECT scale(1.2300::numeric), min_scale(1.2300::numeric);
SELECT trim_scale(1e10::numeric), min_scale(1e10::numeric);
