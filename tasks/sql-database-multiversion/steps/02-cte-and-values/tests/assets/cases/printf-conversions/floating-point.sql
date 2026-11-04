-- PROFILE.md section 2.3: the default precision of %f, %e and %E is 6 and the
-- exponent of %e/%E carries at least two digits. Every value here is exact in
-- binary64 (TEST_INTERFACE.md section 5).
SELECT printf('%.2f|%.3f|%.0f', 3.5, 2.25, 2.0);
SELECT printf('%f', 1.5);
SELECT printf('%e|%E', 1.5, 1.5);
SELECT printf('%g|%g|%g', 0.5, 100.0, 0.25);
