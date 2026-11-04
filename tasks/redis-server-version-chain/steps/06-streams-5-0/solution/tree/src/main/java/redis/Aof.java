package redis;

import java.io.*;
import java.nio.file.*;
import java.util.*;

/**
 * The multi part append only file (redis 7.0, src/aof.c).
 *
 * Since 7.0 the AOF is not a single file any more: `appenddirname` holds a
 * manifest plus one BASE file (an RDB snapshot, or a RESP command stream when
 * aof-use-rdb-preamble is off) and one or more INCR files:
 *
 *   appendonlydir/appendonly.aof.manifest
 *   appendonlydir/appendonly.aof.1.base.rdb
 *   appendonlydir/appendonly.aof.1.incr.aof
 *
 * The JVM cannot fork, so the "background" rewrite is simulated: the dataset is
 * serialized synchronously at fork time (which is exactly what the child would
 * have seen) and the rewrite is only *completed* once the stand-in child is
 * gone.  When a save delay is configured the stand-in is a real sleeping
 * process, so that the test suite can find it with `pgrep -P` and kill it, the
 * way it kills a real rewrite child.
 */
public final class Aof {
    /** server.aof_state */
    public static final int OFF = 0, ON = 1, WAIT_REWRITE = 2;

    /** loadAppendOnlyFiles()/loadSingleAppendOnlyFile() status codes. */
    public static final int AOF_OK = 0, AOF_NOT_EXIST = 1, AOF_EMPTY = 2,
                            AOF_OPEN_ERR = 3, AOF_FAILED = 4, AOF_TRUNCATED = 5;

    static final String BASE_FILE_SUFFIX = ".base";
    static final String INCR_FILE_SUFFIX = ".incr";
    static final String RDB_FORMAT_SUFFIX = ".rdb";
    static final String AOF_FORMAT_SUFFIX = ".aof";
    static final String MANIFEST_NAME_SUFFIX = ".manifest";
    static final String TEMP_FILE_NAME_PREFIX = "temp-";

    public static final char TYPE_BASE = 'b', TYPE_HIST = 'h', TYPE_INCR = 'i';

    private static final int MANIFEST_MAX_LINE = 1024;

    // ------------------------------------------------------------------
    // aofInfo / aofManifest
    // ------------------------------------------------------------------
    public static final class Info {
        public String name;
        public long seq;
        public char type;
        Info() {}
        Info(String n, long s, char t) { name = n; seq = s; type = t; }
        Info dup() { return new Info(name, seq, type); }
    }

    public static final class Manifest {
        public Info base;
        public List<Info> incr = new ArrayList<>();
        public List<Info> history = new ArrayList<>();
        public long currBaseSeq, currIncrSeq;
        public boolean dirty;

        Manifest dup() {
            Manifest m = new Manifest();
            m.currBaseSeq = currBaseSeq;
            m.currIncrSeq = currIncrSeq;
            m.dirty = dirty;
            if (base != null) m.base = base.dup();
            for (Info i : incr) m.incr.add(i.dup());
            for (Info i : history) m.history.add(i.dup());
            return m;
        }
    }

    // ------------------------------------------------------------------
    // the stand-in for a forked child
    // ------------------------------------------------------------------
    /**
     * redis forks for BGSAVE/AOFRW and the suite kills that child to make the
     * operation fail.  `rdb-key-save-delay` is what makes the child slow enough
     * to be killed, so we only need a real process in that case.
     */
    public static final class Child {
        public final long pid;
        final Process proc;
        Child(long pid, Process p) { this.pid = pid; this.proc = p; }
        public boolean running() { return proc != null && proc.isAlive(); }
        /** the signal that killed the child, 0 when it exited on its own */
        public int bysignal() {
            if (proc == null) return 0;
            int v = proc.exitValue();
            return v > 128 ? v - 128 : 0;
        }
        public void kill() { if (proc != null) proc.destroyForcibly(); }
    }

    /** The live stand-in children, so that a shutdown never leaks one. */
    private static final List<Child> LIVE = new ArrayList<>();

    public static Child fork(Server s) {
        Persist.totalForks++;
        long delayUs = s.config.getLong("rdb-key-save-delay");
        long keys = 0;
        if (delayUs > 0 && s.dbs != null) for (Db d : s.dbs) keys += d.size();
        long totalMs = delayUs > 0 ? (delayUs / 1000) * Math.max(keys, 1) : 0;
        if (totalMs <= 0) return new Child(ProcessHandle.current().pid(), null);
        double secs = Math.min(totalMs / 1000.0, 120.0);
        try {
            Process p = new ProcessBuilder("sleep", String.format(Locale.US, "%.3f", secs))
                    .redirectOutput(ProcessBuilder.Redirect.DISCARD)
                    .redirectError(ProcessBuilder.Redirect.DISCARD)
                    .start();
            Child ch = new Child(p.pid(), p);
            LIVE.removeIf(x -> !x.running());
            LIVE.add(ch);
            return ch;
        } catch (Exception e) {
            return new Child(ProcessHandle.current().pid(), null);
        }
    }

    public static void killAllChildren() {
        for (Child c : LIVE) c.kill();
        LIVE.clear();
    }

    // ------------------------------------------------------------------
    // state
    // ------------------------------------------------------------------
    public final Server s;
    public int state = OFF;
    public Manifest manifest = new Manifest();

    public OutputStream out;
    public Buf buf = new Buf(4096);
    public int lastDb = -1;

    /** the simulated rewrite child; -1 when no rewrite is running */
    public long childPid = -1;
    private Child child = null;
    /** the temporary base file the "child" produced */
    private Path childTmp = null;
    private boolean childOk = true;
    public boolean rewriteScheduled = false;
    public long rewriteTimeStart = -1;
    public long lastRewriteTimeSec = -1;

    public long baseSize = 0;          /* server.aof_rewrite_base_size */
    public long currentSize = 0;       /* server.aof_current_size */
    public long lastIncrSize = 0;      /* server.aof_last_incr_size */
    public long lastIncrFsyncOffset = 0;

    /** WAITAOF bookkeeping (7.2): -1 while the AOF is off. */
    public long fsyncedReploff = -1;
    public long fsyncedReploffPending = 0;
    private long lastFsync = 0;

    public long statRewrites = 0;
    public long consecutiveFailures = 0;
    private int nextDelayMinutes = 0;
    private long nextRewriteTime = 0;
    private long curTimestamp = 0;

    /** DEBUG AOF-FLUSH-SLEEP: microseconds to sleep before each AOF write. */
    public long flushSleepUs = 0;

    public String lastWriteStatus = "ok";
    public String lastBgrewriteStatus = "ok";

    public Aof(Server s) { this.s = s; }

    /** kept for the many call sites that only care whether the AOF is on */
    public boolean isEnabled() { return state != OFF; }

    public boolean hasChild() { return childPid != -1; }

    private boolean hasActiveChildProcess() { return childPid != -1 || Persist.bgsaveInProgress; }

    // ------------------------------------------------------------------
    // names and paths
    // ------------------------------------------------------------------
    public String aofFilename() { return s.config.getOr("appendfilename", "appendonly.aof"); }
    public String aofDirname() { return s.config.getOr("appenddirname", "appendonlydir"); }
    public Path aofDir() { return s.resolve(aofDirname()); }
    public Path dirFile(String name) { return aofDir().resolve(name); }
    public String manifestName() { return aofFilename() + MANIFEST_NAME_SUFFIX; }
    public Path manifestPath() { return dirFile(manifestName()); }
    private String tempManifestName() { return TEMP_FILE_NAME_PREFIX + manifestName(); }
    private String tempIncrName() { return TEMP_FILE_NAME_PREFIX + aofFilename() + INCR_FILE_SUFFIX; }

    /** aof.c: the AOF rewrite child writes into the *server* directory. */
    private Path tmpBasePath(long pid) { return s.resolve("temp-rewriteaof-bg-" + pid + ".aof"); }

    /** legacy call site: the file the AOF is currently appending to */
    public Path aofPath() {
        String n = lastIncrName();
        return n == null ? s.resolve(aofFilename()) : dirFile(n);
    }

    private String lastIncrName() {
        if (manifest.incr.isEmpty()) return null;
        return manifest.incr.get(manifest.incr.size() - 1).name;
    }

    // ------------------------------------------------------------------
    // manifest formatting (aofInfoFormat / getAofManifestAsString)
    // ------------------------------------------------------------------
    static boolean needsRepr(String n) {
        for (int i = 0; i < n.length(); i++) {
            char c = n.charAt(i);
            if (c == '\\' || c == '"' || c == '\n' || c == '\r' || c == '\t'
                    || c == 7 || c == '\b' || c < 32 || c > 126 || c == ' ') return true;
        }
        return false;
    }

    static String catRepr(String n) {
        StringBuilder sb = new StringBuilder(n.length() + 8);
        sb.append('"');
        for (int i = 0; i < n.length(); i++) {
            char c = n.charAt(i);
            switch (c) {
                case '\\': case '"': sb.append('\\').append(c); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                case 7:    sb.append("\\a"); break;
                case '\b': sb.append("\\b"); break;
                default:
                    if (c >= 32 && c < 127) sb.append(c);
                    else sb.append(String.format("\\x%02x", (int) c & 0xff));
            }
        }
        return sb.append('"').toString();
    }

    private static void fmt(StringBuilder sb, Info ai) {
        sb.append("file ").append(needsRepr(ai.name) ? catRepr(ai.name) : ai.name)
          .append(" seq ").append(ai.seq)
          .append(" type ").append(ai.type)
          .append('\n');
    }

    public static String manifestAsString(Manifest m) {
        StringBuilder sb = new StringBuilder();
        if (m.base != null) fmt(sb, m.base);
        for (Info i : m.history) fmt(sb, i);
        for (Info i : m.incr) fmt(sb, i);
        return sb.toString();
    }

    private boolean writeManifestFile(String content) {
        try {
            Files.createDirectories(aofDir());
            Path tmp = dirFile(tempManifestName());
            Files.write(tmp, Util.b(content));
            Files.move(tmp, manifestPath(), StandardCopyOption.REPLACE_EXISTING);
            return true;
        } catch (Exception e) {
            s.logWarn("Can't open the AOF manifest file " + tempManifestName() + ": " + e.getMessage());
            return false;
        }
    }

    private boolean persistManifest(Manifest m) {
        if (!m.dirty) return true;
        if (!writeManifestFile(manifestAsString(m))) return false;
        m.dirty = false;
        return true;
    }

    // ------------------------------------------------------------------
    // manifest loading (aofLoadManifestFromDisk / aofLoadManifestFromFile)
    // ------------------------------------------------------------------
    public void loadManifestFromDisk() {
        manifest = new Manifest();
        if (!Files.isDirectory(aofDir())) return;
        if (!Files.exists(manifestPath())) return;
        Manifest am = loadManifestFromFile(manifestPath());
        if (am != null) manifest = am;
    }

    private void manifestError(String err, int linenum, String line) {
        s.logWarn("");
        s.logWarn("*** FATAL AOF MANIFEST FILE ERROR ***");
        s.logWarn("");
        if (line != null) {
            s.logWarn("Reading the manifest file, at line " + linenum);
            s.logWarn(">>> '" + line + "'");
        }
        s.logWarn(err);
        s.logStream.flush();
        Runtime.getRuntime().halt(1);
    }

    private Manifest loadManifestFromFile(Path p) {
        byte[] data;
        try {
            data = Files.readAllBytes(p);
        } catch (IOException e) {
            s.logWarn("Fatal error: can't open the AOF manifest file " + p + " for reading: "
                    + e.getMessage());
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
            return null;
        }

        Manifest am = new Manifest();
        long maxseq = 0;
        int pos = 0, linenum = 0;
        final int n = data.length;

        while (true) {
            if (pos >= n) {
                if (linenum == 0) manifestError("Found an empty AOF manifest", 0, null);
                break;
            }
            /* fgets(buf, MANIFEST_MAX_LINE+1, fp) */
            int end = Math.min(pos + MANIFEST_MAX_LINE, n);
            int nl = -1;
            for (int i = pos; i < end; i++) if (data[i] == '\n') { nl = i; break; }
            String raw = Util.s(data, pos, (nl >= 0 ? nl + 1 : end) - pos);
            pos = (nl >= 0 ? nl + 1 : end);
            linenum++;

            if (raw.length() > 0 && raw.charAt(0) == '#') continue;
            if (nl < 0) manifestError("The AOF manifest file contains too long line", linenum, null);

            String line = trim(raw);
            if (line.isEmpty()) manifestError("Invalid AOF manifest file format", linenum, line);

            List<String> argv;
            try {
                argv = Config.splitArgs(line);
            } catch (RuntimeException e) {
                argv = null;
            }
            if (argv == null || argv.size() < 6 || (argv.size() % 2) != 0)
                manifestError("Invalid AOF manifest file format", linenum, line);

            Info ai = new Info();
            for (int i = 0; i + 1 < argv.size(); i += 2) {
                String k = argv.get(i);
                String v = argv.get(i + 1);
                if (k.equalsIgnoreCase("file")) {
                    ai.name = v;
                    if (v.indexOf('/') >= 0 || v.equals(".") || v.equals(".."))
                        manifestError("File can't be a path, just a filename", linenum, line);
                } else if (k.equalsIgnoreCase("seq")) {
                    ai.seq = atoll(v);
                } else if (k.equalsIgnoreCase("type")) {
                    ai.type = v.isEmpty() ? 0 : v.charAt(0);
                }
            }
            if (ai.name == null || ai.seq == 0 || ai.type == 0)
                manifestError("Invalid AOF manifest file format", linenum, line);

            if (ai.type == TYPE_BASE) {
                if (am.base != null) manifestError("Found duplicate base file information", linenum, line);
                am.base = ai;
                am.currBaseSeq = ai.seq;
            } else if (ai.type == TYPE_HIST) {
                am.history.add(ai);
            } else if (ai.type == TYPE_INCR) {
                if (ai.seq <= maxseq) manifestError("Found a non-monotonic sequence number", linenum, line);
                am.incr.add(ai);
                am.currIncrSeq = ai.seq;
                maxseq = ai.seq;
            } else {
                manifestError("Unknown AOF file type", linenum, line);
            }
        }
        return am;
    }

    private static String trim(String s) {
        int a = 0, b = s.length();
        while (a < b && isTrim(s.charAt(a))) a++;
        while (b > a && isTrim(s.charAt(b - 1))) b--;
        return s.substring(a, b);
    }

    private static boolean isTrim(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

    private static long atoll(String v) {
        long r = 0;
        int i = 0;
        boolean neg = false;
        if (i < v.length() && (v.charAt(i) == '-' || v.charAt(i) == '+')) { neg = v.charAt(i) == '-'; i++; }
        for (; i < v.length(); i++) {
            char c = v.charAt(i);
            if (c < '0' || c > '9') break;
            r = r * 10 + (c - '0');
        }
        return neg ? -r : r;
    }

    // ------------------------------------------------------------------
    // manifest mutation helpers
    // ------------------------------------------------------------------
    private String newBaseNameAndMarkPreAsHistory(Manifest m) {
        if (m.base != null) {
            m.base.type = TYPE_HIST;
            m.history.add(0, m.base);
        }
        String suffix = s.config.getBool("aof-use-rdb-preamble") ? RDB_FORMAT_SUFFIX : AOF_FORMAT_SUFFIX;
        Info ai = new Info(aofFilename() + "." + (++m.currBaseSeq) + BASE_FILE_SUFFIX + suffix,
                m.currBaseSeq, TYPE_BASE);
        m.base = ai;
        m.dirty = true;
        return ai.name;
    }

    private String newIncrName(Manifest m) {
        Info ai = new Info(aofFilename() + "." + (++m.currIncrSeq) + INCR_FILE_SUFFIX + AOF_FORMAT_SUFFIX,
                m.currIncrSeq, TYPE_INCR);
        m.incr.add(ai);
        m.dirty = true;
        return ai.name;
    }

    private String lastIncrNameOrNew(Manifest m) {
        if (m.incr.isEmpty()) return newIncrName(m);
        return m.incr.get(m.incr.size() - 1).name;
    }

    private void markRewrittenIncrAofAsHistory(Manifest m) {
        if (m.incr.isEmpty()) return;
        /* out != null means AOF enabled: skip the last INCR, we write to it. */
        int upto = (out != null) ? m.incr.size() - 1 : m.incr.size();
        for (int i = upto - 1; i >= 0; i--) {
            Info ai = m.incr.remove(i);
            ai.type = TYPE_HIST;
            m.history.add(0, ai);
        }
        m.dirty = true;
    }

    /** aof.c aofDelHistoryFiles() */
    public boolean delHistoryFiles() {
        if (s.config.getBool("aof-disable-auto-gc") || manifest.history.isEmpty()) return true;
        for (Info ai : manifest.history) {
            s.logNotice("Removing the history file " + ai.name + " in the background");
            try { Files.deleteIfExists(dirFile(ai.name)); } catch (IOException ignored) {}
        }
        manifest.history.clear();
        manifest.dirty = true;
        return persistManifest(manifest);
    }

    private void delTempIncrAofFile() {
        s.logNotice("Removing the temp incr aof file " + tempIncrName() + " in the background");
        try { Files.deleteIfExists(dirFile(tempIncrName())); } catch (IOException ignored) {}
    }

    // ------------------------------------------------------------------
    // startup
    // ------------------------------------------------------------------
    /** server.c loadDataFromDisk() for the AOF branch. */
    public void loadOnStartup() {
        state = ON;
        long start = System.currentTimeMillis();
        int ret = loadAppendOnlyFiles();
        if (ret == AOF_FAILED || ret == AOF_OPEN_ERR) {
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        }
        if (ret != AOF_NOT_EXIST) {
            s.logNotice(String.format(Locale.US, "DB loaded from append only file: %.3f seconds",
                    (System.currentTimeMillis() - start) / 1000.0));
        }
    }

    /** aof.c aofOpenIfNeededOnServerStart(). */
    public void init() {
        if (!s.config.getBool("appendonly")) { state = OFF; fsyncedReploff = -1; return; }
        state = ON;
        fsyncedReploff = 0;
        fsyncedReploffPending = s.masterReplOffset;
        try {
            Files.createDirectories(aofDir());
        } catch (IOException e) {
            s.logWarn("Can't open or create append-only dir " + aofDirname() + ": " + e.getMessage());
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        }

        boolean hadIncr = !manifest.incr.isEmpty();
        if (manifest.base == null && !hadIncr) {
            String baseName = newBaseNameAndMarkPreAsHistory(manifest);
            if (!writeBaseFile(dirFile(baseName))) {
                s.logStream.flush();
                Runtime.getRuntime().halt(1);
            }
            s.logNotice("Creating AOF base file " + baseName + " on server start");
        }

        String name = lastIncrNameOrNew(manifest);
        if (!openIncr(dirFile(name), true)) {
            s.logWarn("Can't open the append-only file " + name + ": No such file or directory");
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        }
        if (!persistManifest(manifest)) {
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        }
        lastIncrSize = fileSize(dirFile(name));
        lastIncrFsyncOffset = lastIncrSize;
        lastFsync = System.currentTimeMillis();
        s.logNotice((hadIncr ? "Opening AOF incr file " : "Creating AOF incr file ") + name
                + " on server start");
        updateCurrentSize();
    }

    private boolean openIncr(Path p, boolean append) {
        closeFile();
        try {
            Files.createDirectories(aofDir());
            out = new BufferedOutputStream(new FileOutputStream(p.toFile(), append));
            return true;
        } catch (IOException e) {
            out = null;
            return false;
        }
    }

    private long fileSize(Path p) {
        try { return Files.exists(p) ? Files.size(p) : 0; } catch (IOException e) { return 0; }
    }

    public void updateCurrentSize() {
        long total = 0;
        if (manifest.base != null) total += fileSize(dirFile(manifest.base.name));
        for (Info i : manifest.incr) total += fileSize(dirFile(i.name));
        currentSize = total;
    }

    // ------------------------------------------------------------------
    // loading (aof.c loadAppendOnlyFiles)
    // ------------------------------------------------------------------
    public int loadAppendOnlyFiles() {
        Manifest am = manifest;
        String fname = aofFilename();
        Path old = s.resolve(fname);
        if (Files.exists(old)) {
            if (!Files.isDirectory(aofDir())
                    || (am.base == null && am.incr.isEmpty())
                    || (am.base != null && am.incr.isEmpty()
                        && am.base.name.equals(fname) && !Files.exists(dirFile(fname)))) {
                aofUpgradePrepare(am);
            }
        }

        if (am.base == null && am.incr.isEmpty()) return AOF_NOT_EXIST;

        int totalNum = (am.base != null ? 1 : 0) + am.incr.size();
        long totalSize = 0;
        List<String> names = new ArrayList<>();
        if (am.base != null) names.add(am.base.name);
        for (Info i : am.incr) names.add(i.name);
        for (String nm : names) {
            Path p = dirFile(nm);
            if (!Files.exists(p)) {
                s.logWarn("Unable to obtain the AOF file " + nm
                        + " length. stat: No such file or directory");
                return AOF_FAILED;
            }
            totalSize += fileSize(p);
        }
        if (totalSize == 0) return AOF_EMPTY;

        boolean wasLoading = s.loading;
        s.loading = true;
        int ret = AOF_OK;
        long baseFileSize = 0;
        int aofNum = 0;
        try {
            if (am.base != null) {
                String nm = am.base.name;
                baseFileSize = fileSize(dirFile(nm));
                boolean lastFile = ++aofNum == totalNum;
                long start = System.currentTimeMillis();
                ret = AofLoad.loadSingle(this, nm);
                if (ret == AOF_OK || (ret == AOF_TRUNCATED && lastFile)) {
                    s.logNotice(String.format(Locale.US, "DB loaded from base file %s: %.3f seconds",
                            nm, (System.currentTimeMillis() - start) / 1000.0));
                }
                if (ret == AOF_TRUNCATED && !lastFile) {
                    ret = AOF_FAILED;
                    s.logWarn("Fatal error: the truncated file is not the last file");
                }
                if (ret == AOF_OPEN_ERR || ret == AOF_FAILED) return ret;
            }
            for (Info ai : am.incr) {
                String nm = ai.name;
                boolean lastFile = ++aofNum == totalNum;
                long start = System.currentTimeMillis();
                ret = AofLoad.loadSingle(this, nm);
                if (ret == AOF_OK || (ret == AOF_TRUNCATED && lastFile)) {
                    s.logNotice(String.format(Locale.US, "DB loaded from incr file %s: %.3f seconds",
                            nm, (System.currentTimeMillis() - start) / 1000.0));
                }
                if (ret == AOF_EMPTY) ret = AOF_OK;
                if (ret == AOF_TRUNCATED && !lastFile) {
                    ret = AOF_FAILED;
                    s.logWarn("Fatal error: the truncated file is not the last file");
                }
                if (ret == AOF_OPEN_ERR || ret == AOF_FAILED) return ret;
            }
            currentSize = totalSize;
            baseSize = baseFileSize;
            return ret;
        } finally {
            s.loading = wasLoading;
        }
    }

    /** aof.c aofUpgradePrepare(): move an old single file AOF into the AOF dir. */
    private void aofUpgradePrepare(Manifest am) {
        String fname = aofFilename();
        try {
            Files.createDirectories(aofDir());
        } catch (IOException e) {
            s.logWarn("Can't open or create append-only dir " + aofDirname() + ": " + e.getMessage());
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        }
        am.base = new Info(fname, 1, TYPE_BASE);
        am.currBaseSeq = 1;
        am.dirty = true;
        if (!persistManifest(am)) {
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        }
        try {
            Files.move(s.resolve(fname), dirFile(fname), StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException e) {
            s.logWarn("Error trying to move the old AOF file " + fname + " into dir "
                    + aofDirname() + ": " + e.getMessage());
            s.logStream.flush();
            Runtime.getRuntime().halt(1);
        }
        s.logNotice("Successfully migrated an old-style AOF file (" + fname
                + ") into the AOF directory (" + aofDirname() + ").");
    }

    // ------------------------------------------------------------------
    // CONFIG SET appendonly yes|no  (aof.c startAppendOnly/stopAppendOnly)
    // ------------------------------------------------------------------
    public boolean startAppendOnly() {
        state = WAIT_REWRITE;
        fsyncedReploff = 0;
        fsyncedReploffPending = s.masterReplOffset;
        if (Persist.bgsaveInProgress) {
            rewriteScheduled = true;
            s.logWarn("AOF was enabled but there is already another background operation. "
                    + "An AOF background was scheduled to start when possible.");
        } else if (s.execDepth > 0) {
            rewriteScheduled = true;
            s.logWarn("AOF was enabled during a transaction. An AOF background was scheduled "
                    + "to start when possible.");
        } else {
            if (childPid != -1) {
                s.logWarn("AOF was enabled but there is already an AOF rewriting in background. "
                        + "Stopping background AOF and starting a rewrite now.");
                killRewriteChild();
            }
            if (!startBackgroundRewrite()) {
                state = OFF;
                fsyncedReploff = -1;
                s.logWarn("Redis needs to enable the AOF but can't trigger a background AOF rewrite "
                        + "operation. Check the above logs for more info about the error.");
                return false;
            }
        }
        lastFsync = System.currentTimeMillis();
        lastWriteStatus = "ok";
        return true;
    }

    public void stopAppendOnly() {
        flush();
        closeFile();
        state = OFF;
        lastDb = -1;
        rewriteScheduled = false;
        lastIncrSize = 0;
        lastIncrFsyncOffset = 0;
        fsyncedReploff = -1;
        buf.clear();
        killRewriteChild();
    }

    // ------------------------------------------------------------------
    // the write path
    // ------------------------------------------------------------------
    public void feed(int dbid, byte[][] argv) {
        if (state == OFF) return;
        if (state == WAIT_REWRITE && childPid == -1) return;
        if (s.config.getBool("aof-timestamp-enabled")) {
            long now = System.currentTimeMillis() / 1000L;
            if (curTimestamp < now) {
                curTimestamp = now;
                buf.put("#TS:").putLong(now).crlf();
            }
        }
        /* dbid == -1 means "this command carries no database" (the MULTI that
         * opens a propagated transaction), exactly like replicationFeedSlaves(). */
        if (dbid != -1 && dbid != lastDb) {
            appendCmd(new byte[][]{Util.b("SELECT"), Util.b(Integer.toString(dbid))});
            lastDb = dbid;
        }
        appendCmd(argv);
    }

    private void appendCmd(byte[][] argv) {
        buf.put((byte) '*').putLong(argv.length).crlf();
        for (byte[] a : argv) buf.put((byte) '$').putLong(a.length).crlf().put(a).crlf();
    }

    public void cron() { beforeSleep(); }

    /** server.c beforeSleep(): write the buffer out, then publish the fsync offset. */
    public void beforeSleep() {
        if (state == ON || state == WAIT_REWRITE) flush();
        if (state == ON && fsyncedReploff != -1) fsyncedReploff = fsyncedReploffPending;
    }

    public void flush() {
        String policy = s.config.getOr("appendfsync", "everysec");
        long now = System.currentTimeMillis();
        if (out == null) { buf.clear(); return; }
        if (buf.len == 0) {
            if (lastIncrFsyncOffset != lastIncrSize) fsync(policy, now, true);
            else if (!"no".equalsIgnoreCase(policy)) fsyncedReploffPending = s.masterReplOffset;
            return;
        }
        if (flushSleepUs > 0) {
            /* aof.c flushAppendOnlyFile(): the test suite widens the window
             * between the write and the reply with this. */
            try { Thread.sleep(flushSleepUs / 1000L, (int) (flushSleepUs % 1000L) * 1000); }
            catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
        }
        try {
            out.write(buf.a, 0, buf.len);
            out.flush();
            currentSize += buf.len;
            lastIncrSize += buf.len;
            buf.clear();
            lastWriteStatus = "ok";
        } catch (IOException e) {
            buf.clear();
            lastWriteStatus = "err";
            return;
        }
        fsync(policy, now, false);
    }

    private void fsync(String policy, long now, boolean force) {
        if ("always".equalsIgnoreCase(policy)) {
            lastIncrFsyncOffset = lastIncrSize;
            lastFsync = now;
            fsyncedReploffPending = s.masterReplOffset;
        } else if ("everysec".equalsIgnoreCase(policy) && (force || now - lastFsync >= 1000)) {
            lastIncrFsyncOffset = lastIncrSize;
            lastFsync = now;
            fsyncedReploffPending = s.masterReplOffset;
        }
    }

    private void closeFile() {
        try { if (out != null) { out.flush(); out.close(); } } catch (IOException ignored) {}
        out = null;
    }

    public void flushAndClose() {
        flush();
        closeFile();
        killAllChildren();
    }

    // ------------------------------------------------------------------
    // the rewrite state machine
    // ------------------------------------------------------------------
    /** aof.c openNewIncrAofForAppend(). */
    private boolean openNewIncrAofForAppend() {
        if (state == OFF) return true;
        Manifest tempAm = null;
        String newName;
        if (state == WAIT_REWRITE) {
            newName = tempIncrName();
        } else {
            tempAm = manifest.dup();
            newName = newIncrName(tempAm);
        }
        if (!openIncr(dirFile(newName), false)) {
            s.logWarn("Can't open the append-only file " + newName + ": No such file or directory");
            return false;
        }
        if (tempAm != null && !persistManifest(tempAm)) return false;
        s.logNotice("Creating AOF incr file " + newName + " on background rewrite");
        lastIncrSize = 0;
        lastIncrFsyncOffset = 0;
        if (tempAm != null) manifest = tempAm;
        return true;
    }

    /** aof.c rewriteAppendOnlyFileBackground(): registers the (simulated) child. */
    public boolean startBackgroundRewrite() {
        if (hasActiveChildProcess()) return false;
        try {
            Files.createDirectories(aofDir());
        } catch (IOException e) {
            s.logWarn("Can't open or create append-only dir " + aofDirname() + ": " + e.getMessage());
            lastBgrewriteStatus = "err";
            return false;
        }
        lastDb = -1;
        flush();
        if (!openNewIncrAofForAppend()) {
            lastBgrewriteStatus = "err";
            return false;
        }
        statRewrites++;
        child = fork(s);
        childPid = child.pid;
        childOk = true;
        childTmp = tmpBasePath(childPid);
        /* The "child" sees the dataset as of the fork, so serialize it now. */
        if (!writeBaseFile(childTmp)) childOk = false;
        else s.logNotice("Successfully created the temporary AOF base file " + childTmp.getFileName());
        s.logNotice("Background append only file rewriting started by pid " + childPid);
        rewriteScheduled = false;
        rewriteTimeStart = System.currentTimeMillis() / 1000L;
        return true;
    }

    /** aof.c killAppendOnlyChild(): kill without triggering an error condition. */
    public void killRewriteChild() {
        if (childPid == -1) return;
        s.logNotice("Killing running AOF rewrite child: " + childPid);
        if (child != null) child.kill();
        removeTempFiles();
        child = null;
        childPid = -1;
        childTmp = null;
        rewriteTimeStart = -1;
        if (state == WAIT_REWRITE) { buf.clear(); delTempIncrAofFile(); }
    }

    private void removeTempFiles() {
        if (childTmp != null) {
            try { Files.deleteIfExists(childTmp); } catch (IOException ignored) {}
            try { Files.deleteIfExists(s.resolve("temp-rewriteaof-" + childPid + ".aof")); }
            catch (IOException ignored) {}
        }
    }

    /** serverCron(): reap the pending rewrite, then start a scheduled one. */
    public void rewriteCron() {
        if (childPid != -1) {
            if (child != null && child.running()) return;
            int bysignal = child == null ? 0 : child.bysignal();
            backgroundRewriteDoneHandler(childOk ? 0 : 1, bysignal);
            return;
        }
        if (rewriteScheduled && !Persist.bgsaveInProgress && !rewriteLimited()) {
            startBackgroundRewrite();
        }
    }

    /** aof.c backgroundRewriteDoneHandler(). */
    private void backgroundRewriteDoneHandler(int exitcode, int bysignal) {
        if (bysignal == 0 && exitcode == 0) {
            s.logNotice("Background AOF rewrite terminated with success");
            Manifest tempAm = manifest.dup();
            String newBase = newBaseNameAndMarkPreAsHistory(tempAm);
            boolean ok = true;
            try {
                Files.move(childTmp, dirFile(newBase), StandardCopyOption.REPLACE_EXISTING);
                s.logNotice("Successfully renamed the temporary AOF base file "
                        + childTmp.getFileName() + " into " + newBase);
            } catch (IOException e) {
                s.logWarn("Error trying to rename the temporary AOF base file "
                        + childTmp.getFileName() + " into " + newBase + ": " + e.getMessage());
                ok = false;
            }
            if (ok && state == WAIT_REWRITE) {
                String tempIncr = tempIncrName();
                String newIncr = newIncrName(tempAm);
                try {
                    closeFile();
                    Files.move(dirFile(tempIncr), dirFile(newIncr), StandardCopyOption.REPLACE_EXISTING);
                    s.logNotice("Successfully renamed the temporary AOF incr file " + tempIncr
                            + " into " + newIncr);
                    openIncr(dirFile(newIncr), true);
                } catch (IOException e) {
                    s.logWarn("Error trying to rename the temporary AOF incr file " + tempIncr
                            + " into " + newIncr + ": " + e.getMessage());
                    try { Files.deleteIfExists(dirFile(newBase)); } catch (IOException ignored) {}
                    ok = false;
                }
            }
            if (ok) {
                markRewrittenIncrAofAsHistory(tempAm);
                if (persistManifest(tempAm)) {
                    manifest = tempAm;
                    if (state != OFF) {
                        currentSize = fileSize(dirFile(newBase)) + lastIncrSize;
                        baseSize = currentSize;
                    }
                    delHistoryFiles();
                    lastBgrewriteStatus = "ok";
                    consecutiveFailures = 0;
                    s.logNotice("Background AOF rewrite finished successfully");
                    if (state == WAIT_REWRITE) state = ON;
                } else {
                    try { Files.deleteIfExists(dirFile(newBase)); } catch (IOException ignored) {}
                    ok = false;
                }
            }
            if (!ok) {
                lastBgrewriteStatus = "err";
                consecutiveFailures++;
            }
        } else if (bysignal == 0) {
            lastBgrewriteStatus = "err";
            consecutiveFailures++;
            s.logWarn("Background AOF rewrite terminated with error");
        } else {
            lastBgrewriteStatus = "err";
            consecutiveFailures++;
            s.logWarn("Background AOF rewrite terminated by signal " + bysignal);
        }

        removeTempFiles();
        if (state == WAIT_REWRITE) {
            buf.clear();
            closeFile();
            delTempIncrAofFile();
            /* Schedule a new rewrite: we are still waiting for one to switch
             * the AOF ON (aof.c backgroundRewriteDoneHandler cleanup). */
            rewriteScheduled = true;
        }
        if (rewriteTimeStart != -1) {
            lastRewriteTimeSec = System.currentTimeMillis() / 1000L - rewriteTimeStart;
            rewriteTimeStart = -1;
        }
        child = null;
        childPid = -1;
        childTmp = null;
    }

    /** aof.c aofRewriteLimited(). */
    public boolean rewriteLimited() {
        long unixtime = System.currentTimeMillis() / 1000L;
        if (consecutiveFailures < 3) {
            nextDelayMinutes = 0;
            nextRewriteTime = 0;
            return false;
        }
        if (nextRewriteTime != 0) {
            if (unixtime < nextRewriteTime) return true;
            nextRewriteTime = 0;
            return false;
        }
        nextDelayMinutes = (nextDelayMinutes == 0) ? 1 : nextDelayMinutes * 2;
        if (nextDelayMinutes > 60) nextDelayMinutes = 60;
        nextRewriteTime = unixtime + nextDelayMinutes * 60L;
        s.logWarn("Background AOF rewrite has repeatedly failed and triggered the limit, "
                + "will retry in " + nextDelayMinutes + " minutes");
        return true;
    }

    /** serverCron(): auto-aof-rewrite-percentage / auto-aof-rewrite-min-size. */
    public void autoRewriteCron() {
        if (state != ON || hasActiveChildProcess()) return;
        long perc = s.config.getLong("auto-aof-rewrite-percentage");
        long minSize = s.config.getLong("auto-aof-rewrite-min-size");
        if (perc <= 0) return;
        if (currentSize <= minSize) return;
        long base = baseSize != 0 ? baseSize : 1;
        long growth = (currentSize * 100 / base) - 100;
        if (growth >= perc && !rewriteLimited()) {
            s.logNotice("Starting automatic rewriting of AOF on " + growth + "% growth");
            startBackgroundRewrite();
        }
    }

    // ------------------------------------------------------------------
    // writing a BASE file (aof.c rewriteAppendOnlyFile)
    // ------------------------------------------------------------------
    private boolean writeBaseFile(Path p) {
        try {
            if (s.config.getBool("aof-use-rdb-preamble")) {
                Rdb.saveFile(s, p);
            } else {
                try (OutputStream o = new BufferedOutputStream(Files.newOutputStream(p))) {
                    Buf b = new Buf(1 << 16);
                    if (s.config.getBool("aof-timestamp-enabled")) {
                        b.put("#TS:").putLong(System.currentTimeMillis() / 1000L).crlf();
                    }
                    for (Db d : s.dbs) {
                        if (d.dict.isEmpty()) continue;
                        writeCmd(b, Util.b("SELECT"), Util.b(Integer.toString(d.id)));
                        for (Map.Entry<Key, RObj> e : new ArrayList<>(d.dict.entrySet())) {
                            AofRewrite.writeObject(s, b, d, e.getKey(), e.getValue());
                            if (b.len > 1 << 20) { o.write(b.a, 0, b.len); b.clear(); }
                        }
                    }
                    o.write(b.a, 0, b.len);
                }
            }
            return true;
        } catch (Exception e) {
            s.logWarn("Error writing the AOF base file: " + e);
            try { Files.deleteIfExists(p); } catch (IOException ignored) {}
            return false;
        }
    }

    /** The whole rewrite, performed synchronously (DEBUG / shutdown paths). */
    public void rewriteNow() {
        if (!startBackgroundRewrite()) return;
        if (child != null) child.kill();
        child = null;
        backgroundRewriteDoneHandler(childOk ? 0 : 1, 0);
    }

    static void writeCmd(Buf b, byte[]... argv) {
        b.put((byte) '*').putLong(argv.length).crlf();
        for (byte[] a : argv) b.put((byte) '$').putLong(a.length).crlf().put(a).crlf();
    }

    // ------------------------------------------------------------------
    // DEBUG LOADAOF
    // ------------------------------------------------------------------
    public boolean load() {
        loadManifestFromDisk();
        delHistoryFiles();
        int ret = loadAppendOnlyFiles();
        return ret == AOF_OK || ret == AOF_TRUNCATED || ret == AOF_EMPTY;
    }
}
