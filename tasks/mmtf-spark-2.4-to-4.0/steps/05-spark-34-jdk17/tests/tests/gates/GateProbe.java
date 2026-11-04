package gate;

import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.net.URL;
import java.net.URLStreamHandler;
import java.net.URLStreamHandlerFactory;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Properties;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;

import org.apache.spark.SparkConf;
import org.apache.spark.scheduler.SparkListener;
import org.apache.spark.scheduler.SparkListenerApplicationEnd;
import org.apache.spark.scheduler.SparkListenerApplicationStart;
import org.apache.spark.scheduler.SparkListenerEvent;
import org.apache.spark.scheduler.SparkListenerJobStart;

/**
 * A SparkListener that reports what a running session actually was.
 *
 * Steps 3 to 8 are all claims about runtime rather than about source: that a
 * job goes through the SQL engine at all, that adaptive execution is on, that
 * ANSI is on *by default* rather than by an override, that the session is a
 * Connect session. None of those can be read off a file, and each of them can
 * be asserted in a configuration the job then overwrites -- so they are read
 * from inside the job, from the session the job actually ran.
 *
 * Attached without touching the candidate's code:
 *
 *   spark-submit --conf spark.driver.extraClassPath=/gates/gate-probe.jar \
 *                --conf spark.extraListeners=gate.GateProbe
 *
 * and it writes one TSV of facts per application to the file descriptor in
 * $GATE_PROBE_FD, or to $GATE_PROBE_DIR, or to the exact path in
 * $GATE_PROBE_OUT. The replay uses the first of those and nothing else: a
 * descriptor is a pipe the runner drains, and a job that is never told a path
 * cannot go back and rewrite what this listener said about it (round-3 review,
 * R5). When $GATE_PROBE_NONCE is set, every row goes out with that token in
 * front of it. The descriptor is then one pipe shared by every job of the
 * replay and read by a process the candidate's uid cannot signal or write
 * around, and the token is what tells the reader whose rows these are: it is
 * generated per job, it reaches only that job's environment, and a row
 * carrying anybody else's token -- or none -- is a row this listener did not
 * write (round-3 review, R5 Blocker 1). The two path forms stay for the standalone probes, which run the
 * verifier's own class and have no candidate in them. With none of the three
 * set it writes nothing at all and
 * costs a few counters: a probe that crashed a job it was only observing would
 * be worse than no probe, and every method below is written on that principle.
 *
 * Fact kinds:
 *
 *   probe    <name>   <value>       version, session class, conf source
 *   conf     <key>    <value>       effective, including engine defaults
 *   setconf  <key>    <value>       explicitly set in SparkConf -- the override
 *   event    <name>   <count>
 *   job      <app>#<id> <exec-id>   one row per Spark job, "-" when it ran
 *                                   outside a SQL execution
 *   error    <what>   <why>
 *
 * `conf` and `setconf` are the distinction step 8 turns on. "ANSI is on" is a
 * `conf` fact; "ANSI was switched on by hand" is a `setconf` fact; the step
 * requires the first without the second.
 */
public class GateProbe extends SparkListener {

    /** Settings whose *effective* value some step asks about. */
    private static final String[] WATCH = {
        "spark.sql.adaptive.enabled",
        "spark.sql.adaptive.coalescePartitions.enabled",
        "spark.sql.ansi.enabled",
        "spark.sql.datetime.java8API.enabled",
        "spark.sql.legacy.allowUntypedScalaUDF",
        "spark.sql.legacy.charVarcharAsString",
        // The other half of the 3.1 output move step 3 lands on: with this on,
        // a nested array of structs casts to string the 2.4 way ("[[N, 661]]"
        // rather than "[{N, 661}]"). Read for the same reason as the key
        // below, and read here rather than inferred from the jar because the
        // property's name can be assembled out of pieces at run time and the
        // engine is the only reader that does not care how it was spelled.
        "spark.sql.legacy.castComplexTypesToString.enabled",
        // Introduced at 3.1 (SPARK-13860), which is the release that made
        // stddev/variance over a single-row group render null rather than
        // NaN. Watched rather than inferred from spark.version because the
        // three states a step needs to tell apart are three different
        // values here: "<unset>" on a Spark that predates the change,
        // "false" on one where the new semantics are in force, and "true"
        // where a tree switched them back off. A version read collapses the
        // last two.
        "spark.sql.legacy.statisticalAggregate",
        "spark.sql.legacy.timeParserPolicy",
        "spark.sql.parquet.int96RebaseModeInWrite",
        "spark.sql.session.timeZone",
        "spark.sql.shuffle.partitions",
        "spark.sql.sources.partitionOverwriteMode",
        "spark.sql.storeAssignmentPolicy",
        "spark.sql.timestampType",
    };

    private final SparkConf conf;
    private final AtomicInteger jobs = new AtomicInteger();
    /**
     * One entry per Spark job: its id, and the SQL execution it belonged to.
     *
     * `event sqlExecutionStart <n>` already counts the executions an
     * application started, and a count cannot answer the question step 2 asks,
     * which is per job rather than per application: an application that starts
     * one execution and four jobs may have run three of them outside it. The
     * execution id is a *job property* -- SQLExecution.withNewExecutionId puts
     * spark.sql.execution.id into the thread's local properties, and every job
     * the thread submits under it inherits it -- so it is read here, from the
     * job, and not reconstructed from event order.
     *
     * Unbounded in principle and bounded in practice: the largest job in this
     * census starts 40 Spark jobs. A queue rather than a list because
     * onJobStart is called from the scheduler's event thread while dump() may
     * be running on the shutdown hook's.
     */
    private final ConcurrentLinkedQueue<String> jobExec = new ConcurrentLinkedQueue<String>();
    private final AtomicInteger sqlExecutions = new AtomicInteger();
    /**
     * Executions the adaptive optimiser actually revised a plan for.
     *
     * Step 3 gates on adaptive execution, and 3.2 turns it on by default -- so
     * "the setting is true" is a fact about the version, not about the tree,
     * and the step's own instruction forbids forging settings. What cannot be
     * forged is the optimiser reporting that it re-planned a stage. Matched on
     * the simple name for the same reason as the start event above:
     * SparkListenerSQLAdaptiveExecutionUpdate is developer API in a package
     * that has moved, and a count is all that is needed.
     */
    private final AtomicInteger adaptiveUpdates = new AtomicInteger();
    /**
     * The ids of the SQL executions this application actually started.
     *
     * `spark.sql.execution.id` is a *local property*, and local properties are
     * writable: `sc.setLocalProperty("spark.sql.execution.id", "0")` puts the
     * id of an execution that never happened onto every job a thread submits,
     * and a gate that read only the job's property would pay full reward for a
     * tree that migrated nothing. Measured, not supposed: the scratch main of
     * NOTES 6.2.5 does exactly that and the gate believed it.
     *
     * So the set is kept, and a job's claim is checked against it at dump time
     * rather than at onJobStart -- the resolution then does not depend on the
     * order two event types arrive in, which is a property of the listener bus
     * and not of this file.
     *
     * A ConcurrentHashMap rather than a set because Java 7 has no concurrent
     * set literal and this class compiles at source level 8 against 2.4.
     */
    private final ConcurrentHashMap<String, Boolean> startedExecs =
            new ConcurrentHashMap<String, Boolean>();
    private final AtomicInteger dumped = new AtomicInteger();
    private volatile String appId = "unknown";

    /**
     * The session, held from the first callback that can see one.
     *
     * SparkSession.active() is a thread local backed by a default that stop()
     * clears, and the dump runs either during stop() or after it. Holding a
     * reference is what keeps the conf readable in both cases: RuntimeConfig
     * reads a settings map, and a stopped session still has its map.
     */
    private volatile Object sessionRef;

    /**
     * Every value a watched setting was seen to hold *while a session was
     * live*, as "key\tvalue".
     *
     * The dump is the wrong place to read a setting and be sure of it. A job
     * that stops its context, or that dies and leaves the shutdown hook to do
     * the reporting, still has a session object -- this class holds one on
     * purpose -- but RuntimeConfig can no longer be built from it, and the
     * read comes back as an exception rather than as a value. One of the 140
     * recorded jobs is exactly that.
     *
     * Reporting the exception in the value's place is not a small
     * inaccuracy. Every prohibition on a setting is written `absent` over the
     * values it forbids, so a job that set a forbidden flag, did its work
     * under it and then arranged not to be readable would report a value that
     * matches nothing and would be prohibited by nothing: the candidate would
     * be switching the gate off rather than passing it.
     *
     * So the settings are read at each job start, when the session is by
     * construction alive, and the dump reports everything that was ever read
     * rather than whatever survives to the end.
     */
    private final ConcurrentHashMap<String, Boolean> seenConf =
            new ConcurrentHashMap<String, Boolean>();

    /**
     * What happened when this probe claimed java.net.URL's handler factory:
     * "installed", "already-set", or the exception that stopped it.
     *
     * Reading a session's settings is not free. RuntimeConfig is reached
     * through sessionState, sessionState is reached through sharedState, and
     * *constructing* SharedState registers Hadoop's FsUrlStreamHandlerFactory
     * for every scheme Hadoop has a FileSystem for -- ftp among them
     * (spark.sql.defaultUrlStreamHandlerFactory.enabled, 3.0 onward, SPARK-
     * 25694). URL.setURLStreamHandlerFactory also clears the JDK's cache of
     * already-resolved handlers, so this reaches back and changes URLs the
     * program opened before the probe ever read anything.
     *
     * That is a probe changing the answer, which is the one thing a probe may
     * not do, and it was measured rather than reasoned about: the golden tree
     * of step 4 replays 140/140 unprobed and 139/140 probed. The job that
     * moves is PerformHadoopUpdate, whose HadoopUpdate.getLists() reads an FTP
     * *directory listing* through java.net.URL. FTPFileSystem opens files, not
     * directories, so the second of its two passes got a FileNotFoundException
     * where the first had got six dates -- and getLists() catches, so the job
     * still exits 0 and still says "update success". A silent half.
     *
     * The handed tree never showed it: 62 of the 78 mains build a context and
     * no session, there is no sharedState to construct, and the factory is
     * never registered. Which is exactly the shape check-suite.sh's PROBE knob
     * exists to catch -- "run the label twice, with and without, and the two
     * verdicts must agree" -- and from step 4 on, every tree has sessions.
     *
     * So the probe takes the factory first, in its own constructor, before any
     * of the candidate's code past SparkContext's own construction runs, and
     * hands back null for every protocol: null means "use the JDK's builtin",
     * which is the world all 140 recordings were taken in. Spark's later
     * attempt then throws, SharedState catches it and logs a warning, and
     * nothing else changes. The outcome is a fact rather than an assumption,
     * because a defence that silently failed to install would put the finding
     * back without putting the red back.
     */
    private volatile String urlFactory = "not-attempted";

    public GateProbe(SparkConf conf) {
        this.conf = conf == null ? new SparkConf(false) : conf;
        keepJdkUrlHandlers();
        // onApplicationEnd only fires when something calls stop(), and 78
        // main() classes are not 78 classes that call stop(). The hook is what
        // makes the probe report on the other ones.
        Runtime.getRuntime().addShutdownHook(new Thread() {
            @Override public void run() { dump(); }
        });
    }

    public GateProbe() { this(new SparkConf(false)); }

    /**
     * Claim java.net.URL's one global handler factory, and give it away again.
     *
     * A factory that returns null for every protocol is the JDK's own
     * behaviour: URL.getURLStreamHandler falls through to the builtin lookup
     * when the factory declines. Holding it is what stops anything else --
     * SharedState, in practice -- from installing one that does not.
     *
     * Called from the constructor because that is the last moment that is
     * still early enough. Spark instantiates spark.extraListeners inside
     * SparkContext's own construction, so this runs before the first line of
     * the program that follows `new JavaSparkContext(...)`, and long before
     * anything asks a session for a setting.
     */
    private void keepJdkUrlHandlers() {
        try {
            URL.setURLStreamHandlerFactory(new URLStreamHandlerFactory() {
                @Override
                public URLStreamHandler createURLStreamHandler(String protocol) {
                    return null;
                }
            });
            urlFactory = "installed";
        } catch (Error e) {
            // setURLStreamHandlerFactory throws a bare Error when one is
            // already set. Reported, not swallowed: if something got there
            // first, the run is not the one the recordings were taken in and
            // the gate should be able to see that in the facts.
            urlFactory = "already-set";
        } catch (Throwable t) {
            urlFactory = "<" + t.getClass().getSimpleName() + ">";
        }
    }

    @Override
    public void onApplicationStart(SparkListenerApplicationStart start) {
        capture();
        snapshotConf();
        try {
            scala.Option<String> id = start.appId();
            if (id.isDefined()) {
                appId = id.get();
            }
        } catch (Throwable t) {
            // The id only names the output file; the facts do not need it.
        }
    }

    @Override
    public void onJobStart(SparkListenerJobStart jobStart) {
        capture();
        snapshotConf();
        jobs.incrementAndGet();
        jobExec.add(jobStart.jobId() + "\t" + executionId(jobStart));
    }

    /**
     * The SQL execution this job belonged to, or "-" if it belonged to none.
     *
     * properties() is null for a job submitted with no local properties at all,
     * which is what an RDD action outside the SQL engine looks like from here;
     * so is a properties object without the key. The two are the same answer
     * and are reported as the same answer. Any other failure is reported as
     * itself rather than as "-", because a probe that turned its own breakage
     * into the finding the gate is looking for would be worse than no probe.
     */
    private String executionId(SparkListenerJobStart jobStart) {
        try {
            Properties p = jobStart.properties();
            if (p == null) {
                return "-";
            }
            String v = p.getProperty("spark.sql.execution.id");
            return v == null || v.isEmpty() ? "-" : v;
        } catch (Throwable t) {
            return "<" + t.getClass().getSimpleName() + ">";
        }
    }

    @Override
    public void onOtherEvent(SparkListenerEvent event) {
        capture();
        // Matched on the simple name rather than by importing the class.
        // SparkListenerSQLExecutionStart lives in
        // org.apache.spark.sql.execution.ui, it is developer API, and it has
        // moved and changed shape between 2.4 and 4.0. The name has not, and a
        // count is all step 3 needs: "this job went through the SQL engine".
        if (event != null
                && event.getClass().getSimpleName()
                        .equals("SparkListenerSQLAdaptiveExecutionUpdate")) {
            adaptiveUpdates.incrementAndGet();
        }
        if (event != null
                && event.getClass().getSimpleName().equals("SparkListenerSQLExecutionStart")) {
            sqlExecutions.incrementAndGet();
            // Reflection for the same reason the match above is on a name: the
            // class is developer API in a package that has moved. `executionId`
            // is the one accessor it has carried unchanged from 2.4 to 4.0, and
            // a version where it has not is reported as an unread id rather
            // than as an absent one -- see collect(), where a set that does not
            // account for every start event stops accusing anybody.
            try {
                Object id = event.getClass().getMethod("executionId").invoke(event);
                if (id != null) {
                    startedExecs.put(String.valueOf(id), Boolean.TRUE);
                }
            } catch (Throwable t) {
                // Left to the count/size disagreement in collect().
            }
        }
    }

    @Override
    public void onApplicationEnd(SparkListenerApplicationEnd end) {
        dump();
    }

    private void capture() {
        if (sessionRef == null) {
            Object s = liveSession();
            if (s != null) {
                sessionRef = s;
            }
        }
    }

    /**
     * Read the watched settings through a live session and remember them.
     *
     * Distinct values, and every distinct value is kept rather than the last
     * one: a key that read `true` while one job ran and `false` while the next
     * one did has held both, and a gate that forbids one of them has to see
     * it. A read that went wrong is not a value and is not recorded; if no
     * read of a key ever succeeds, collect() says so and reconstructs it.
     *
     * The cost is 15 reflective reads per job start. The busiest application
     * in this codebase's census starts 69 jobs.
     */
    private void snapshotConf() {
        Object session = liveSession();
        if (session == null) {
            session = sessionRef;
        }
        if (session == null) {
            return;
        }
        for (String key : WATCH) {
            String v = runtimeConf(session, key);
            if (v.startsWith("<") && !"<unset>".equals(v)) {
                continue;
            }
            seenConf.put(key + "\t" + v, Boolean.TRUE);
        }
    }

    /**
     * The name the job facts are keyed by.
     *
     * `spark.app.name` is the obvious choice and it is not unique: this
     * codebase has three mains whose app name is `CustomReportDemo`, two of
     * them by a copy-paste that was never fixed upstream, and two scored rows
     * that are the same class run with different arguments. A gate reading 140
     * probe files concatenated would then have one key for three applications.
     * So the replay passes the row name it already knows in GATE_PROBE_LABEL,
     * and the app name is only the fallback for a standalone probe that has no
     * row (session-probe.sh). `probe app.name` still reports what the
     * application called itself, so the disagreement stays visible.
     */
    private String label() {
        String v = System.getenv("GATE_PROBE_LABEL");
        if (v != null && !v.trim().isEmpty()) {
            return v.trim().replaceAll("[^A-Za-z0-9._-]", "_");
        }
        return conf.get("spark.app.name", "unknown");
    }

    private File target() {
        // A descriptor the runner opened and still holds, reached through
        // /proc/self/fd. Writing through it costs the job nothing it would not
        // have paid writing a file, and it takes the output's location out of
        // the child's environment, which is the only place a candidate could
        // have read it from.
        String fd = System.getenv("GATE_PROBE_FD");
        if (fd != null && fd.matches("[0-9]{1,4}")) {
            return new File("/proc/self/fd/" + fd);
        }
        String exact = System.getenv("GATE_PROBE_OUT");
        if (exact != null && !exact.isEmpty()) {
            return new File(exact);
        }
        String dir = System.getenv("GATE_PROBE_DIR");
        if (dir == null || dir.isEmpty()) {
            return null;
        }
        new File(dir).mkdirs();
        return new File(dir, appId.replaceAll("[^A-Za-z0-9._-]", "_") + ".tsv");
    }

    /** Idempotent: the shutdown hook and onApplicationEnd both call it. */
    private synchronized void dump() {
        if (dumped.getAndIncrement() != 0) {
            return;
        }
        File out = target();
        if (out == null) {
            return;
        }
        List<String> facts = new ArrayList<String>();
        try {
            collect(facts);
        } catch (Throwable t) {
            facts.add("error\tcollect\t" + t.getClass().getName());
        }
        Collections.sort(facts);
        // The per-job token, in front of every row. Matched rather than
        // trusted: this string is going into a stream the reader splits on it,
        // and an environment variable holding a tab would put rows in another
        // job's file. Absent or malformed, the rows go out bare, which is the
        // shape the standalone probes and the authoring replays read.
        String nonce = System.getenv("GATE_PROBE_NONCE");
        String tag = (nonce != null && nonce.matches("[0-9a-f]{16,64}"))
                ? nonce + "\t" : "";
        PrintWriter w = null;
        try {
            w = new PrintWriter(out, "UTF-8");
            for (String f : facts) {
                w.println(tag + f);
            }
        } catch (IOException e) {
            // Nothing to do about it and nowhere safe to say so: the job is
            // exiting. A lost probe file is a missing fact, which the gate
            // reports as a missing fact; throwing here would be a failed job.
        } finally {
            if (w != null) {
                w.close();
            }
        }
    }

    private void fact(List<String> out, String kind, String a, String b) {
        out.add(kind + "\t" + a + "\t"
                + (b == null ? "" : b.replace("\t", " ").replace("\n", " ")));
    }

    private void collect(List<String> facts) {
        int started = sqlExecutions.get();
        fact(facts, "event", "jobStart", String.valueOf(jobs.get()));
        fact(facts, "event", "sqlExecutionStart", String.valueOf(started));
        fact(facts, "event", "adaptiveUpdate", String.valueOf(adaptiveUpdates.get()));
        String app = conf.get("spark.app.name", "unknown");
        String jobKey = label();
        // The set is authoritative only when it accounts for every start event
        // seen. If reflection failed on this Spark build the two numbers
        // disagree, and then a job's claim is reported as it stands: a probe
        // that could not read the ids must not turn that into an accusation.
        boolean complete = startedExecs.size() == started;
        fact(facts, "event", "sqlExecutionIds", String.valueOf(startedExecs.size()));
        // Keyed by the replay's row name as well as by job id, because a gate
        // reads the probe files of 140 applications concatenated and job ids
        // start again at 0 in each one. The name is the one thing in the key a
        // human can use: a red row has to say which class ran the job.
        for (String row : jobExec) {
            int tab = row.indexOf('\t');
            String raw = row.substring(tab + 1);
            String val = raw;
            if (complete && !"-".equals(raw) && !raw.startsWith("<")
                    && !startedExecs.containsKey(raw)) {
                val = "forged:" + raw;
            }
            fact(facts, "job", jobKey + "#" + row.substring(0, tab), val);
        }
        fact(facts, "probe", "app.name", app);
        fact(facts, "probe", "app.label", jobKey);
        fact(facts, "probe", "app.id", appId);
        fact(facts, "probe", "scala.version", scalaVersion());
        fact(facts, "probe", "java.version",
             System.getProperty("java.specification.version", "?"));
        fact(facts, "probe", "master", conf.get("spark.master", "<unset>"));
        fact(facts, "probe", "urlfactory", urlFactory);

        // Explicitly set: SparkConf holds what the submit line, the system
        // properties and the code put there. Not defaults -- that is the point.
        scala.Tuple2<String, String>[] all = conf.getAll();
        for (scala.Tuple2<String, String> kv : all) {
            if (kv._1().startsWith("spark.sql.")) {
                fact(facts, "setconf", kv._1(), kv._2());
            }
        }

        Object live = liveSession();
        Object session = live != null ? live : sessionRef;
        fact(facts, "probe", "session.present", String.valueOf(session != null));
        if (session != null) {
            fact(facts, "probe", "conf.source", live != null ? "active" : "captured");
            fact(facts, "probe", "session.class", session.getClass().getName());
            fact(facts, "probe", "spark.version", callString(session, "version"));
            // One last reading, on the same footing as the ones taken at each
            // job start: if it works it is another observation, and if it does
            // not it is discarded rather than reported as the setting.
            snapshotConf();
        }

        // 62 of this codebase's 78 mains never name SparkSession -- they build
        // a JavaSparkContext and stay on RDDs -- so for most of the census
        // there is no session to ask, and a probe that reported nothing for
        // those would leave steps 4, 5 and 8 asserting over whichever handful
        // of jobs happened to have one. That is a mean over an almost-empty
        // set wearing a different hat.
        //
        // So the settings are reconstructed the way a session would have
        // computed them: a bare SQLConf, which carries every entry's default,
        // with the job's own spark.sql.* settings laid over the top. Without
        // that overlay a job that switched ANSI back off would still be
        // reported as ANSI-on, and step 8's gate would pass on the tree it
        // exists to fail.
        //
        // The same reconstruction is the last resort for a *session* job whose
        // every read of some key failed. It is a weaker answer than a reading
        // -- it cannot see a value set through RuntimeConfig, which never
        // reaches SparkConf -- so the count of keys answered this way is
        // reported as a fact of its own rather than left to be inferred from
        // the values.
        Object defaults = bareSqlConf();
        if (defaults == null && seenConf.isEmpty()) {
            fact(facts, "error", "session", "no SparkSession and no SQLConf on the classpath");
            return;
        }
        if (defaults != null) {
            for (scala.Tuple2<String, String> kv : all) {
                if (kv._1().startsWith("spark.sql.")) {
                    sqlConfSet(defaults, kv._1(), kv._2());
                }
            }
        }
        if (session == null) {
            fact(facts, "probe", "conf.source", "defaults");
        }
        int rebuilt = 0;
        for (String key : WATCH) {
            // Sorted, because a HashMap's iteration order is not a thing a
            // scored comparison may depend on.
            List<String> vals = new ArrayList<String>();
            String stem = key + "\t";
            for (String pair : seenConf.keySet()) {
                if (pair.startsWith(stem)) {
                    vals.add(pair.substring(stem.length()));
                }
            }
            Collections.sort(vals);
            if (vals.isEmpty()) {
                String v = defaults == null ? "<no-sqlconf>" : sqlConfGet(defaults, key);
                // `<unset>` is an answer: this Spark has no such setting, so
                // no job of it can have been running under one. Any other
                // angle-bracketed value is the absence of an answer, and a
                // `conf` row is graded -- four of them by prohibitions priced
                // at the whole reward. So an undetermined setting is reported
                // as the probe's own failure, which common.tsv's probe-clean
                // is `absent` over, and no `conf` row is written for it. No
                // rule then has to carry an exclusion for a reading that did
                // not happen, and no candidate can turn one into a value.
                if (v.startsWith("<") && !"<unset>".equals(v)) {
                    fact(facts, "error", "conf." + key, v);
                    continue;
                }
                vals.add(v);
                rebuilt++;
            }
            for (String v : vals) {
                fact(facts, "conf", key, v);
            }
        }
        fact(facts, "probe", "conf.rebuilt", String.valueOf(rebuilt));
    }

    /** A settings map with every entry's default and nothing set. */
    private Object bareSqlConf() {
        try {
            return Class.forName("org.apache.spark.sql.internal.SQLConf")
                    .getDeclaredConstructor().newInstance();
        } catch (Throwable t) {
            return null;
        }
    }

    private void sqlConfSet(Object conf, String key, String value) {
        try {
            conf.getClass().getMethod("setConfString", String.class, String.class)
                .invoke(conf, key, value);
        } catch (Throwable t) {
            // A value this Spark rejects for this key. The setconf fact has
            // already recorded what was asked for; the conf fact will show
            // whatever the engine would really have used.
        }
    }

    private String sqlConfGet(Object conf, String key) {
        try {
            Object v = conf.getClass().getMethod("getConfString", String.class)
                           .invoke(conf, key);
            return v == null ? "" : v.toString();
        } catch (Throwable t) {
            return classify(t);
        }
    }

    /**
     * SparkSession.active(), then getDefaultSession(), by reflection.
     *
     * Not because the calls are exotic -- they are static forwarders and Java
     * can see them -- but because this class is compiled once per rung against
     * that rung's Spark, and a probe that fails to *load* takes the job with
     * it. Reflection turns "this Spark does not have that" into a fact.
     */
    private Object liveSession() {
        Class<?> cls;
        try {
            cls = Class.forName("org.apache.spark.sql.SparkSession");
        } catch (Throwable t) {
            return null;
        }
        try {
            Object s = cls.getMethod("active").invoke(null);
            if (s != null) {
                return s;
            }
        } catch (Throwable t) {
            // active() throws when there is none; fall through to the default.
        }
        try {
            Object opt = cls.getMethod("getDefaultSession").invoke(null);
            scala.Option<?> o = (scala.Option<?>) opt;
            if (o != null && o.isDefined()) {
                return o.get();
            }
        } catch (Throwable t) {
            // Neither exists or neither is set: reported as an error fact.
        }
        return null;
    }

    private String scalaVersion() {
        try {
            return String.valueOf(Class.forName("scala.util.Properties")
                    .getMethod("versionNumberString").invoke(null));
        } catch (Throwable t) {
            return "<" + t.getClass().getSimpleName() + ">";
        }
    }

    private String callString(Object target, String method) {
        try {
            Object v = target.getClass().getMethod(method).invoke(target);
            return v == null ? "" : v.toString();
        } catch (Throwable t) {
            return "<" + t.getClass().getSimpleName() + ">";
        }
    }

    /**
     * session.conf().get(key), with "not a setting this Spark knows" separated
     * from "the read went wrong".
     *
     * The two-argument get(key, default) looks like the obvious call and is a
     * trap: SQLConf.getConfString runs the *default* through the entry's value
     * converter before it looks anything up, so asking for
     * spark.sql.ansi.enabled with a sentinel default throws
     * IllegalArgumentException on the sentinel and the probe reports an error
     * for every setting that exists. The one-argument form has no such
     * problem; it throws only when the key is unknown and unset, which is a
     * distinct answer and is reported as one.
     */
    private String runtimeConf(Object session, String key) {
        try {
            Object rc = session.getClass().getMethod("conf").invoke(session);
            Object v = rc.getClass().getMethod("get", String.class).invoke(rc, key);
            return v == null ? "" : v.toString();
        } catch (Throwable t) {
            return classify(t);
        }
    }

    /**
     * "This Spark has no such setting" told apart from "the read went wrong".
     *
     * Spark 4 throws SparkNoSuchElementException, Spark 2.4 and 3.x throw
     * java.util.NoSuchElementException; matched on the name so one probe
     * source reads both.
     */
    private String classify(Throwable t) {
        Throwable c = t;
        while (c instanceof java.lang.reflect.InvocationTargetException
                && c.getCause() != null) {
            c = c.getCause();
        }
        if (c.getClass().getSimpleName().contains("NoSuchElement")) {
            return "<unset>";
        }
        return "<" + c.getClass().getSimpleName() + ">";
    }
}
