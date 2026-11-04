-- R07.18: julianday() with modifiers, and timediff().
--
-- Before this checkpoint julianday() takes one argument only -- a second
-- argument is answered "epoch or julianday function with > 1 arguments.
-- Modifiers are not yet supported" -- and timediff() is not a known function
-- at all. Both arrive here, so every line below changes from a refusal to a
-- value at this step.
--
-- Every argument is a literal date string. Nothing here reads the clock, so
-- the case is deterministic and can be compared byte for byte; the wall-clock
-- forms ('now', current_timestamp) are deliberately absent.
--
-- Each expected value is the one stock sqlite3 returns for the identical
-- statement, so this case is a differential against the reference SQLite as
-- well as a regression against the earlier checkpoint.
--
-- Two boundaries this case deliberately stops at, both measured 2026-09-21
-- against v0.0.19 and the pinned stock 3.50.2, and both stated in
-- TEST_INTERFACE.md under "Three boundaries the graded cases stop at":
--
--   * timediff() spans are all inside one day here. Across a month boundary
--     the two engines disagree -- timediff('2024-03-01','2024-01-01') is
--     +0000-02-00 00:00:00.000 from stock and +0000-00-60 00:00:00.000 from
--     this engine, which carries the span in days rather than promoting it to
--     months. That is reference behaviour at this checkpoint, so widening the
--     case would grade a disagreement the release has not resolved; R07.18's
--     stock-agreement clause is scoped to the intervals below instead.
--
--   * every julianday() argument below lands on a half-day, so every value
--     concatenated here is a non-integral real. An integral one renders
--     differently through || : 'jd='||julianday('2025-01-01 12:00:00') is
--     jd=2460677 from this engine and jd=2460677.0 from stock, although a
--     bare SELECT of the same call prints 2460677.0 from both. PROFILE rule 4
--     governs result-column rendering, not concatenation, so nothing fixes
--     that form and no line below crosses it.
SELECT 'jd-plain:'||julianday('2025-01-01');
SELECT 'jd-day:'||julianday('2025-01-01','+1 day');
SELECT 'jd-days-back:'||julianday('2025-01-01','-10 days');
SELECT 'jd-month-overflow:'||julianday('2025-01-31','+1 month');
SELECT 'jd-year-leap:'||julianday('2024-02-29','+1 year');
SELECT 'jd-start-of-month:'||julianday('2025-03-17','start of month');
SELECT 'jd-start-of-year:'||julianday('2025-03-17','start of year');
SELECT 'jd-start-of-day:'||julianday('2025-03-17 13:45:01','start of day');
SELECT 'jd-chain:'||julianday('2025-03-17','start of month','+3 months','-1 day');
SELECT 'jd-weekday:'||julianday('2025-03-17','weekday 0');
SELECT 'td-day:'||timediff('2025-01-02 00:00:00','2025-01-01 00:00:00');
SELECT 'td-hour:'||timediff('2025-01-01 05:00:00','2025-01-01 00:00:00');
SELECT 'td-negative:'||timediff('2025-01-01 00:00:00','2025-01-02 00:00:00');
SELECT 'td-second:'||timediff('2025-01-01 00:00:10','2025-01-01 00:00:01');
SELECT 'td-zero:'||timediff('2025-01-01 00:00:00','2025-01-01 00:00:00');
