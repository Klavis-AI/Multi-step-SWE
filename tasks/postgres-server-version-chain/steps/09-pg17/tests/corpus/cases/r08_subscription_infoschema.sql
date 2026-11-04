SELECT column_name FROM information_schema.columns
  WHERE table_name = 'pg_subscription' AND column_name IN
        ('suborigin','subtwophasestate','subdisableonerr','subrunasowner','subpasswordrequired')
  ORDER BY column_name;
SELECT count(*) FROM information_schema.columns WHERE table_name = 'pg_subscription';
SELECT column_name FROM information_schema.columns WHERE table_name = 'pg_stat_subscription_stats' ORDER BY 1;
