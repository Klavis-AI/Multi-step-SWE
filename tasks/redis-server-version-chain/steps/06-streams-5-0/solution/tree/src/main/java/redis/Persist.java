package redis;

/**
 * RDB saving: the `save <sec> <changes>` rules and the (simulated) background
 * save child.  The JVM cannot fork, so BGSAVE registers a child and the save
 * itself is performed by the next cron tick, which is what keeps
 * rdb_bgsave_in_progress observable the way redis's is.  When a save delay is
 * configured the child is a real (sleeping) process, so that the test suite can
 * find it with `pgrep -P` and kill it.
 */
public final class Persist {
    public static long lastSaveDirty = 0;
    public static long lastSaveTime = System.currentTimeMillis();
    public static String lastBgsaveStatus = "ok";
    public static boolean bgsaveInProgress = false;
    public static boolean bgsaveScheduled = false;
    public static long bgsaveStart = 0;
    public static long lastBgsaveTimeSec = -1;
    public static long rdbSaves = 0;
    /** INFO stats total_forks: every simulated fork bumps it. */
    public static long totalForks = 0;
    public static long childPid = -1;
    private static Aof.Child child = null;
    private static long dirtyBeforeBgsave = 0;

    /** rdb.c rdbSaveBackground(): register the child, the work happens in cron. */
    public static void startBgSave(Server s) {
        if (bgsaveInProgress) return;
        bgsaveInProgress = true;
        bgsaveScheduled = false;
        bgsaveStart = System.currentTimeMillis();
        dirtyBeforeBgsave = s.dirty;
        child = Aof.fork(s);
        childPid = child.pid;
        s.logNotice("Background saving started by pid " + childPid);
    }

    /** serverCron(): the "child" completes here. */
    public static void cron(Server s) {
        if (bgsaveInProgress) {
            if (child != null && child.running()) return;
            int bysignal = child == null ? 0 : child.bysignal();
            if (bysignal != 0) {
                lastBgsaveStatus = "err";
                s.logWarn("Background saving terminated by signal " + bysignal);
            } else {
                try {
                    Rdb.saveFile(s, s.rdbPath());
                    lastSaveDirty = dirtyBeforeBgsave;
                    lastSaveTime = System.currentTimeMillis();
                    lastBgsaveStatus = "ok";
                    rdbSaves++;
                    s.lastSave = System.currentTimeMillis() / 1000L;
                    s.logNotice("Background saving terminated with success");
                } catch (Exception e) {
                    lastBgsaveStatus = "err";
                    s.logWarn("Background saving error: " + e);
                }
            }
            lastBgsaveTimeSec = (System.currentTimeMillis() - bgsaveStart) / 1000;
            bgsaveInProgress = false;
            child = null;
            childPid = -1;
            return;
        }
        if (bgsaveScheduled && !s.aof.hasChild()) startBgSave(s);
    }

    public static void killBgSaveChild() {
        if (!bgsaveInProgress) return;
        if (child != null) child.kill();
        child = null;
        childPid = -1;
        bgsaveInProgress = false;
    }

    public static void maybeBgSave(Server s) {
        if (bgsaveInProgress || s.aof.hasChild()) return;
        String save = s.config.getOr("save", "");
        if (save == null || save.trim().isEmpty()) return;
        String[] t = save.trim().split("\\s+");
        long now = System.currentTimeMillis();
        for (int i = 0; i + 1 < t.length; i += 2) {
            long secs, changes;
            try { secs = Long.parseLong(t[i]); changes = Long.parseLong(t[i + 1]); }
            catch (NumberFormatException e) { continue; }
            if (s.dirty - lastSaveDirty >= changes && now - lastSaveTime >= secs * 1000L) {
                s.logNotice(changes + " changes in " + secs + " seconds. Saving...");
                startBgSave(s);
                return;
            }
        }
    }
}
