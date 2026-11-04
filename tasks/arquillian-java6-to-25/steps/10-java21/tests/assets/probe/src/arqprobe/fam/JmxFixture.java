package arqprobe.fam;

import java.io.File;
import java.io.InputStream;
import java.io.ObjectInputStream;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import javax.management.MBeanOperationInfo;
import javax.management.MBeanServer;
import javax.management.MBeanServerFactory;
import javax.management.ObjectName;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.jmx.Executors;
import arqprobe.fam.jmx.Log;
import arqprobe.fam.jmx.Runners;

import org.jboss.arquillian.protocol.jmx.JMXMethodExecutor;
import org.jboss.arquillian.protocol.jmx.JMXTestRunner;
import org.jboss.arquillian.protocol.jmx.JMXTestRunnerMBean;
import org.jboss.arquillian.spi.ExceptionProxy;
import org.jboss.arquillian.spi.TestMethodExecutor;
import org.jboss.arquillian.spi.TestResult;

/**
 * The JMX protocol: how a test method crosses from the client into a container
 * and how its result comes back.
 *
 * Two classes and one hand-rolled service loader. {@code JMXMethodExecutor}
 * runs on the client: it reduces a test method to two strings, finds or creates
 * an {@code MBeanServer}, proxies {@code JMXTestRunnerMBean} onto it and reads
 * an object stream back. {@code JMXTestRunner} runs in the container: it looks
 * up a {@code TestRunner} through {@code TestRunners}, asks its
 * {@code TestClassLoader} for the test class, runs the method and marshalls
 * the {@code TestResult}. Everything between the two sides is either a string,
 * an {@code ObjectName} or a serialized {@code ExceptionProxy}, and all three
 * are recorded here.
 *
 * <h3>Where the runner comes from</h3>
 *
 * {@code runTestMethodInternal} asks {@code TestRunners.getTestRunner} for the
 * class loader that defined {@code JMXTestRunner}, and no jar in this tree
 * registers a {@code TestRunner} there -- so on the scored class path the
 * shipped answer is always a {@code FAILED} result carrying
 * {@code NoSuchElementException}. That is recorded, and so is everything the
 * lookup does when a runner <em>is</em> declared, by loading
 * {@code JMXTestRunner} in a loader that has a service file: see
 * {@link arqprobe.fam.jmx.Runners}.
 *
 * <p>The lookup is {@code org.jboss.arquillian.spi.util.ServiceLoader}, not the
 * JDK's, and it is forty lines with three observable quirks: it discards the
 * result of {@code line.trim()}, it keeps the space in front of an inline
 * {@code #} comment, and it swallows every per-line failure to stderr and
 * carries on. All three are recorded as behaviour, because a migration that
 * "tidies" them changes what a malformed service file does.
 *
 * <h3>What is deliberately not recorded</h3>
 *
 * {@code TestResult} carries two {@code System.currentTimeMillis()} readings,
 * one set by its constructor and one by {@code JMXMethodExecutor}'s
 * {@code finally}. Neither value is ever written down; only their ordering is,
 * and only where the ordering is the point. Stack traces go the same way: the
 * proxy carries one, and its frames are the JDK's business.
 *
 * <p>Code-driven family -- the fixture name selects the scenario.
 */
public class JmxFixture implements Fixture
{
   /** The test class whose name travels over the wire, and the method on it. */
   private static final String SAMPLE = Executors.Sample.class.getName();

   private final String name;

   public JmxFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      Log.reset();
      List<MBeanServer> before = Runners.snapshot();
      try
      {
         if (!(shippedFixture(r) || runnerFixture(r) || loadingFixture(r)
               || mbeanFixture(r) || executorFixture(r) || marshalFixture(r)))
         {
            throw new IllegalArgumentException("unknown jmx fixture: " + name);
         }
      }
      finally
      {
         // Every server this fixture caused to exist, and no other: the factory
         // registry is global, so a leak here is the next fixture's "one
         // already exists" and would read as a codebase change.
         Runners.releaseNew(before);
      }
   }

   // ------------------------------------------------------------------------
   // what the tree does as shipped: no TestRunner on the application loader
   // ------------------------------------------------------------------------

   private boolean shippedFixture(Rec r) throws Throwable
   {
      if ("shipped-local".equals(name))
      {
         // The lookup comes before the class load, so the TestClassLoader is
         // never asked. An empty log is the evidence for that ordering.
         JMXTestRunner runner = new JMXTestRunner(new Runners.Recording());
         result(r, "result", runner.runTestMethodLocal(SAMPLE, "ok"), true);
         log(r);
         return true;
      }
      if ("shipped-remote".equals(name))
      {
         JMXTestRunner runner = new JMXTestRunner(new Runners.Recording());
         result(r, "result", read(runner.runTestMethodRemote(SAMPLE, "ok")), true);
         log(r);
         return true;
      }
      if ("shipped-remote-twice".equals(name))
      {
         // Each call marshalls into its own byte array; nothing is cached and
         // nothing is shared, which is what lets a container answer two
         // clients.
         JMXTestRunner runner = new JMXTestRunner(new Runners.Recording());
         InputStream a = runner.runTestMethodRemote(SAMPLE, "ok");
         InputStream b = runner.runTestMethodRemote(SAMPLE, "alsoOk");
         r.kv("streams.same", Boolean.valueOf(a == b));
         result(r, "first", read(a), true);
         result(r, "second", read(b), true);
         return true;
      }
      if ("shipped-default-classloader".equals(name))
      {
         // Null installs the anonymous default, which loads through
         // JMXTestRunner's own loader rather than through a deployment.
         JMXTestRunner runner = new JMXTestRunner(null);
         result(r, "result", runner.runTestMethodLocal(SAMPLE, "ok"), true);
         log(r);
         return true;
      }
      if ("mbean-interface-shape".equals(name))
      {
         // The standard-MBean naming convention is load-bearing here: the
         // server finds the management interface by appending "MBean" to the
         // class name, so a rename of either half unregisters the protocol.
         shape(r, "runner", JMXTestRunner.class);
         shape(r, "mbean", JMXTestRunnerMBean.class);
         r.kv("convention", Boolean.valueOf(
               (JMXTestRunner.class.getName() + "MBean").equals(JMXTestRunnerMBean.class.getName())));
         return true;
      }
      if ("object-name".equals(name))
      {
         ObjectName oname = new ObjectName(JMXTestRunnerMBean.OBJECT_NAME);
         r.kv("constant", JMXTestRunnerMBean.OBJECT_NAME);
         r.kv("canonical", oname.getCanonicalName());
         r.kv("domain", oname.getDomain());
         r.kv("keys", oname.getKeyPropertyListString());
         r.kv("pattern", Boolean.valueOf(oname.isPattern()));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // which TestRunner the service file names
   // ------------------------------------------------------------------------

   private boolean runnerFixture(Rec r) throws Throwable
   {
      if ("runner-passed".equals(name))
      {
         through(r, Runners.isolated(Runners.Passing.class));
         return true;
      }
      if ("runner-failed".equals(name))
      {
         through(r, Runners.isolated(Runners.Failing.class));
         return true;
      }
      if ("runner-skipped".equals(name))
      {
         through(r, Runners.isolated(Runners.Skipping.class));
         return true;
      }
      if ("runner-throws".equals(name))
      {
         // A runner that does not come back at all. The catch(Throwable) turns
         // it into a FAILED result rather than letting it cross the protocol.
         through(r, Runners.isolated(Runners.Throwing.class));
         return true;
      }
      if ("runner-returns-null".equals(name))
      {
         // Nothing between the runner and the client checks for null, so the
         // null reaches runTestMethodLocal's caller intact.
         through(r, Runners.isolated(Runners.Nulling.class));
         return true;
      }
      if ("runner-two-providers".equals(name))
      {
         through(r, Runners.isolated(Runners.Passing.class, Runners.AlsoPassing.class));
         return true;
      }
      if ("runner-same-class-twice".equals(name))
      {
         // The loader collects instances in a LinkedHashSet, and two instances
         // of a class that does not override equals are two members -- so the
         // same provider named twice is "multiple TestRunners".
         through(r, Runners.isolated(Runners.Passing.class, Runners.Passing.class));
         return true;
      }
      if ("runner-missing-class".equals(name))
      {
         through(r, Runners.isolated("arqprobe.fam.jmx.NoSuchRunner\n"));
         return true;
      }
      if ("runner-not-a-runner".equals(name))
      {
         through(r, Runners.isolated(Runners.NotARunner.class.getName() + "\n"));
         return true;
      }
      if ("runner-no-default-ctor".equals(name))
      {
         through(r, Runners.isolated(Runners.NeedsAnArgument.class.getName() + "\n"));
         return true;
      }
      if ("runner-empty-service-file".equals(name))
      {
         through(r, Runners.isolated(""));
         return true;
      }
      if ("runner-no-service-file".equals(name))
      {
         through(r, Runners.isolated((String) null));
         return true;
      }
      if ("runner-comments-only".equals(name))
      {
         through(r, Runners.isolated("# the runner is supplied by the container\n#\n"));
         return true;
      }
      if ("runner-trailing-comment".equals(name))
      {
         // The comment is cut at the '#' and the space before it is kept, so
         // the loader asks for a class name with a trailing blank.
         through(r, Runners.isolated(Runners.Passing.class.getName() + " # the default\n"));
         return true;
      }
      if ("runner-leading-space".equals(name))
      {
         // line.trim() is called and its result thrown away.
         through(r, Runners.isolated("  " + Runners.Passing.class.getName() + "\n"));
         return true;
      }
      if ("runner-blank-lines".equals(name))
      {
         through(r, Runners.isolated("\n\n" + Runners.Passing.class.getName() + "\n\n"));
         return true;
      }
      if ("runner-isolation-holds".equals(name))
      {
         // The point of the isolating loader: JMXTestRunner is a different
         // class, and everything it hands back is not. A TestResult the
         // isolated runner produced has to be this fixture's own TestResult or
         // the whole family would be measuring the harness.
         ClassLoader loader = Runners.isolated(Runners.Passing.class);
         JMXTestRunnerMBean runner = Runners.runner(loader, new Runners.Recording());
         r.kv("runner.loader", Runners.loaderOf(runner));
         r.kv("runner.class", runner.getClass().getName());
         TestResult result = runner.runTestMethodLocal(SAMPLE, "ok");
         r.kv("result.class.same", Boolean.valueOf(result.getClass() == TestResult.class));
         r.kv("mbean.iface.same", Boolean.valueOf(
               JMXTestRunnerMBean.class.isInstance(runner)));
         result(r, "result", result, true);
         log(r);
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // which class the TestClassLoader is asked for
   // ------------------------------------------------------------------------

   private boolean loadingFixture(Rec r) throws Throwable
   {
      if ("load-recording".equals(name))
      {
         // With a runner present the load does happen, and the log shows the
         // order: class first, then the method call.
         through(r, Runners.isolated(Runners.Passing.class), new Runners.Recording(), SAMPLE, "ok");
         return true;
      }
      if ("load-refusing".equals(name))
      {
         through(r, Runners.isolated(Runners.Passing.class), new Runners.Refusing(), SAMPLE, "ok");
         return true;
      }
      if ("load-returns-null".equals(name))
      {
         // Nothing checks, so the runner is handed a null Class.
         through(r, Runners.isolated(Runners.Passing.class), new Runners.Nothing(), SAMPLE, "ok");
         return true;
      }
      if ("load-default-resolves".equals(name))
      {
         // The default loader delegates to JMXTestRunner's own, which is the
         // isolating one, which delegates everything but the runner upward.
         through(r, Runners.isolated(Runners.Passing.class), null, SAMPLE, "ok");
         return true;
      }
      if ("load-default-missing".equals(name))
      {
         through(r, Runners.isolated(Runners.Passing.class), null,
               "arqprobe.fam.jmx.NoSuchTestClass", "ok");
         return true;
      }
      if ("load-nested-class".equals(name))
      {
         through(r, Runners.isolated(Runners.Passing.class), new Runners.Recording(),
               Executors.Nested.Deeper.class.getName(), "ok");
         return true;
      }
      if ("load-primitive-name".equals(name))
      {
         through(r, Runners.isolated(Runners.Passing.class), null, "int", "ok");
         return true;
      }
      if ("load-array-name".equals(name))
      {
         through(r, Runners.isolated(Runners.Passing.class), null, "[Ljava.lang.String;", "ok");
         return true;
      }
      if ("load-empty-name".equals(name))
      {
         through(r, Runners.isolated(Runners.Passing.class), null, "", "ok");
         return true;
      }
      if ("load-null-name".equals(name))
      {
         through(r, Runners.isolated(Runners.Passing.class), new Runners.Recording(), null, "ok");
         return true;
      }
      if ("method-name-null".equals(name))
      {
         // The method name is a string all the way down; nothing resolves it
         // here, so a null one reaches the runner untouched.
         through(r, Runners.isolated(Runners.Passing.class), new Runners.Recording(), SAMPLE, null);
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // registration
   // ------------------------------------------------------------------------

   private boolean mbeanFixture(Rec r) throws Throwable
   {
      if (!name.startsWith("mbean-"))
      {
         return false;
      }
      final MBeanServer server = "mbean-register-null-server".equals(name)
            || "mbean-unregister-null-server".equals(name)
            ? null : MBeanServerFactory.createMBeanServer();
      final ObjectName oname = new ObjectName(JMXTestRunnerMBean.OBJECT_NAME);
      final JMXTestRunner runner = new JMXTestRunner(new Runners.Recording());

      if ("mbean-register".equals(name))
      {
         r.kv("registered.before", Boolean.valueOf(server.isRegistered(oname)));
         r.value("returned", () -> runner.registerMBean(server));
         r.kv("registered.after", Boolean.valueOf(server.isRegistered(oname)));
         r.kv("count", Integer.valueOf(server.getMBeanCount().intValue()));
         return true;
      }
      if ("mbean-register-twice".equals(name))
      {
         runner.registerMBean(server);
         r.attempt("again", () -> runner.registerMBean(server));
         r.kv("registered", Boolean.valueOf(server.isRegistered(oname)));
         return true;
      }
      if ("mbean-register-two-runners".equals(name))
      {
         // One name per server, so a second deployment in the same container
         // cannot register its own runner.
         final JMXTestRunner other = new JMXTestRunner(new Runners.Recording());
         runner.registerMBean(server);
         r.attempt("other", () -> other.registerMBean(server));
         return true;
      }
      if ("mbean-unregister".equals(name))
      {
         runner.registerMBean(server);
         r.kv("registered.before", Boolean.valueOf(server.isRegistered(oname)));
         r.attempt("unregister", () -> runner.unregisterMBean(server));
         r.kv("registered.after", Boolean.valueOf(server.isRegistered(oname)));
         return true;
      }
      if ("mbean-unregister-not-registered".equals(name))
      {
         // Guarded by isRegistered, so this is a no-op rather than a throw --
         // which is what makes an unconditional teardown safe.
         r.kv("registered.before", Boolean.valueOf(server.isRegistered(oname)));
         r.attempt("unregister", () -> runner.unregisterMBean(server));
         r.kv("registered.after", Boolean.valueOf(server.isRegistered(oname)));
         return true;
      }
      if ("mbean-register-null-server".equals(name))
      {
         r.attempt("register", () -> runner.registerMBean(null));
         return true;
      }
      if ("mbean-unregister-null-server".equals(name))
      {
         r.attempt("unregister", () -> runner.unregisterMBean(null));
         return true;
      }
      if ("mbean-info".equals(name))
      {
         // What a remote client sees of the protocol: the operations the
         // server will accept under that name, and their signatures.
         runner.registerMBean(server);
         r.kv("class", server.getMBeanInfo(oname).getClassName());
         List<String> ops = new ArrayList<String>();
         for (MBeanOperationInfo op : server.getMBeanInfo(oname).getOperations())
         {
            StringBuilder sb = new StringBuilder(op.getReturnType()).append(' ')
                  .append(op.getName()).append('(');
            for (int i = 0; i < op.getSignature().length; i++)
            {
               sb.append(i == 0 ? "" : ",").append(op.getSignature()[i].getType());
            }
            ops.add(sb.append(')').toString());
         }
         r.sorted("operations", ops);
         r.kv("attributes", Integer.valueOf(server.getMBeanInfo(oname).getAttributes().length));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // the client side
   // ------------------------------------------------------------------------

   private boolean executorFixture(Rec r) throws Throwable
   {
      if ("executor-contract-shape".equals(name))
      {
         shape(r, "executor", JMXMethodExecutor.class);
         return true;
      }
      if ("executor-null-argument".equals(name))
      {
         r.attempt("invoke", () -> new JMXMethodExecutor().invoke(null));
         return true;
      }
      if ("executor-no-mbean".equals(name))
      {
         // A server exists but nothing is registered under the runner's name.
         // The failure becomes a FAILED result, not a throw: the executor
         // catches Throwable.
         MBeanServerFactory.createMBeanServer();
         Executors.Stub e = stub("ok");
         invoke(r, e, false);
         return true;
      }
      if ("executor-round-trip".equals(name))
      {
         // The whole protocol in one call: two strings out, an object stream
         // back. The log is the proof that what travelled was the names --
         // the executor never ran the method itself.
         MBeanServer server = MBeanServerFactory.createMBeanServer();
         register(server, Runners.Passing.class);
         Executors.Stub e = stub("ok");
         invoke(r, e, true);
         return true;
      }
      if ("executor-nested-class-name".equals(name))
      {
         // A nested test class is named with a '$', and that is the string the
         // container is asked to load.
         MBeanServer server = MBeanServerFactory.createMBeanServer();
         register(server, Runners.Passing.class);
         Executors.Stub e = new Executors.Stub(new Executors.Nested.Deeper(),
               Executors.method(Executors.Nested.Deeper.class, "ok"));
         invoke(r, e, true);
         return true;
      }
      if ("executor-failed-round-trip".equals(name))
      {
         MBeanServer server = MBeanServerFactory.createMBeanServer();
         register(server, Runners.Failing.class);
         invoke(r, stub("ok"), true);
         return true;
      }
      if ("executor-null-result".equals(name))
      {
         // The runner returns null, the stream carries a null, and the
         // finally block dereferences it. The NPE escapes invoke entirely --
         // there is no catch left to turn it into a result.
         MBeanServer server = MBeanServerFactory.createMBeanServer();
         register(server, Runners.Nulling.class);
         invoke(r, stub("ok"), true);
         return true;
      }
      if ("executor-finds-existing-server".equals(name))
      {
         MBeanServer server = MBeanServerFactory.createMBeanServer();
         register(server, Runners.Passing.class);
         List<MBeanServer> before = Runners.snapshot();
         invoke(r, stub("ok"), true);
         r.kv("created.by.invoke", Integer.valueOf(Runners.createdSince(before)));
         return true;
      }
      if ("executor-creates-server".equals(name))
      {
         // Nothing to find, so one is made -- and never released, which is the
         // leak the family brackets against.
         List<MBeanServer> before = Runners.snapshot();
         r.kv("servers.preexisting", Boolean.valueOf(!before.isEmpty()));
         invoke(r, stub("ok"), false);
         r.kv("created.by.invoke", Integer.valueOf(Runners.createdSince(before)));
         return true;
      }
      if ("executor-multiple-servers".equals(name))
      {
         // findMBeanServer answers in creation order and the executor takes
         // the first, so a runner registered on the second is invisible.
         MBeanServerFactory.createMBeanServer();
         MBeanServer second = MBeanServerFactory.createMBeanServer();
         register(second, Runners.Passing.class);
         List<MBeanServer> before = Runners.snapshot();
         invoke(r, stub("ok"), false);
         r.kv("created.by.invoke", Integer.valueOf(Runners.createdSince(before)));
         return true;
      }
      if ("executor-instance-null".equals(name))
      {
         // Both accessors are read before the try block, so what they raise
         // leaves invoke instead of becoming a FAILED result.
         hostile(r, new Executors.Empty(true));
         return true;
      }
      if ("executor-method-null".equals(name))
      {
         hostile(r, new Executors.Empty(false));
         return true;
      }
      if ("executor-accessor-throws".equals(name))
      {
         hostile(r, new Executors.Hostile());
         return true;
      }
      if ("executor-end-after-start".equals(name))
      {
         // Two wall-clock readings, one per side of the protocol. Their values
         // are never written down; only that the second is not before the
         // first, which is all the code promises.
         MBeanServer server = MBeanServerFactory.createMBeanServer();
         register(server, Runners.Passing.class);
         TestResult result = new JMXMethodExecutor().invoke(stub("ok"));
         r.kv("start.set", Boolean.valueOf(result.getStart() > 0L));
         r.kv("end.set", Boolean.valueOf(result.getEnd() > 0L));
         r.kv("end.not.before.start", Boolean.valueOf(result.getEnd() >= result.getStart()));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // what crosses the wire
   // ------------------------------------------------------------------------

   private boolean marshalFixture(Rec r) throws Throwable
   {
      if ("marshal-round-trip".equals(name))
      {
         TestResult result = remote(Runners.Passing.class, SAMPLE, "ok");
         result(r, "result", result, true);
         r.kv("start.set", Boolean.valueOf(result.getStart() > 0L));
         r.kv("end.zero", Boolean.valueOf(result.getEnd() == 0L));
         return true;
      }
      if ("marshal-proxy-fields".equals(name))
      {
         // The proxy, not the throwable, is what is serialized: a class name,
         // a message and a stack trace. The trace is the JDK's business and is
         // recorded only as "there is one".
         TestResult result = remote(Runners.Throwing.class, SAMPLE, "ok");
         ExceptionProxy proxy = result.getExceptionProxy();
         r.kv("proxy", proxy == null ? "<null>" : "present");
         r.kv("proxy.hasException", Boolean.valueOf(proxy.hasException()));
         r.kv("proxy.cause", proxy.getCause() == null ? "<null>" : proxy.getCause().getClass().getName());
         r.thrown("rebuilt", result.getThrowable());
         r.kv("rebuilt.trace.present", Boolean.valueOf(result.getThrowable().getStackTrace().length > 0));
         return true;
      }
      if ("marshal-assertion-error".equals(name))
      {
         // What a failing JUnit test produces, and the one rebuild that has
         // to work. AssertionError's only String constructor takes Object,
         // so the proxy reaches it through (String, Throwable) -- public
         // since Java 7, and the first of the three signatures it tries. A
         // migration that narrowed that search would turn every assertion
         // failure in the suite into an ArquillianProxyException carrying the
         // text instead of the error, which is what marshal-unrebuildable
         // shows the fallback looking like.
         TestResult result = remote(Runners.Failing.class, SAMPLE, "ok");
         r.kv("status", result.getStatus());
         r.kv("proxy.className", result.getExceptionProxy().hasException() ? "present" : "<none>");
         r.thrown("rebuilt", result.getThrowable());
         r.kv("rebuilt.is.assertion", Boolean.valueOf(
               result.getThrowable() instanceof AssertionError));
         return true;
      }
      if ("marshal-non-serializable".equals(name))
      {
         // Awkward holds a plain Object and could not be serialized itself.
         // It crosses anyway, because only its name and message travel -- and
         // it is rebuilt through its (String) constructor, so the object on
         // this side is a different one with the same story.
         JMXTestRunnerMBean runner = Runners.runner(
               Runners.isolated(Runners.Unmarshallable.class), new Runners.Recording());
         InputStream stream = runner.runTestMethodRemote(SAMPLE, "ok");
         r.kv("marshalled", "ok");
         TestResult result = read(stream);
         result(r, "result", result, true);
         r.kv("rebuilt.class", result.getThrowable().getClass().getName());
         return true;
      }
      if ("marshal-unrebuildable".equals(name))
      {
         // No (String,Throwable), no (String,Exception), no (String): the
         // proxy gives up and hands over an ArquillianProxyException that
         // quotes the original class, message and reason.
         TestResult result = remote(Runners.Stubborning.class, SAMPLE, "ok");
         r.kv("status", result.getStatus());
         r.thrown("rebuilt", result.getThrowable());
         return true;
      }
      if ("marshal-stream-once".equals(name))
      {
         // One object per stream. A client that reads twice gets the end of
         // the array, which is how a truncated response would look too.
         JMXTestRunnerMBean runner = Runners.runner(
               Runners.isolated(Runners.Passing.class), new Runners.Recording());
         final ObjectInputStream ois = new ObjectInputStream(runner.runTestMethodRemote(SAMPLE, "ok"));
         result(r, "first", (TestResult) ois.readObject(), true);
         r.value("second", () -> ois.readObject());
         return true;
      }
      if ("marshal-status-only".equals(name))
      {
         // A SKIPPED result has no throwable at all, so the proxy is null and
         // the stream carries nothing but the enum.
         TestResult result = remote(Runners.Skipping.class, SAMPLE, "ok");
         result(r, "result", result, true);
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // helpers
   // ------------------------------------------------------------------------

   /** Run one method through an isolated runner with the ordinary loader and inputs. */
   private static void through(Rec r, ClassLoader loader) throws Throwable
   {
      through(r, loader, new Runners.Recording(), SAMPLE, "ok");
   }

   private static void through(Rec r, ClassLoader loader, JMXTestRunner.TestClassLoader classes,
         String className, String methodName) throws Throwable
   {
      JMXTestRunnerMBean runner = Runners.runner(loader, classes);
      r.kv("classes", classes == null ? "<default>" : classes.getClass().getSimpleName());
      result(r, "result", runner.runTestMethodLocal(className, methodName), true);
      log(r);
   }

   /** The client side, recording what the executor produced and what it did not do. */
   private static void invoke(Rec r, Executors.Stub e, boolean fullMessages)
   {
      TestResult result;
      try
      {
         result = new JMXMethodExecutor().invoke(e);
      }
      catch (Throwable t)
      {
         r.kv("invoke", "threw");
         if (fullMessages)
         {
            r.thrown("invoke.ex", t);
         }
         else
         {
            r.thrownTypes("invoke.ex", t);
         }
         r.kv("ran.locally", Boolean.valueOf(e.invoked));
         log(r);
         return;
      }
      r.kv("invoke", "ok");
      result(r, "result", result, fullMessages);
      r.kv("ran.locally", Boolean.valueOf(e.invoked));
      log(r);
   }

   /** An executor whose accessors misbehave: whatever they raise leaves invoke. */
   private static void hostile(Rec r, final TestMethodExecutor e)
   {
      r.attempt("invoke", () -> new JMXMethodExecutor().invoke(e));
   }

   private static Executors.Stub stub(String method) throws Exception
   {
      return new Executors.Stub(new Executors.Sample(), Executors.method(Executors.Sample.class, method));
   }

   /** Register an isolated runner's MBean on the given server. */
   private static void register(MBeanServer server, Class<?> provider) throws Throwable
   {
      Runners.register(Runners.runner(Runners.isolated(provider), new Runners.Recording()), server);
   }

   /** Marshall a result through runTestMethodRemote and read it back. */
   private static TestResult remote(Class<?> provider, String className, String methodName)
         throws Throwable
   {
      JMXTestRunnerMBean runner = Runners.runner(Runners.isolated(provider), new Runners.Recording());
      return read(runner.runTestMethodRemote(className, methodName));
   }

   private static TestResult read(InputStream in) throws Exception
   {
      return (TestResult) new ObjectInputStream(in).readObject();
   }

   /**
    * A result as the codebase means it: the status, and the throwable's chain.
    * Never the two timestamps, which are wall-clock readings.
    */
   private static void result(Rec r, String key, TestResult result, boolean fullMessages)
   {
      if (result == null)
      {
         r.kv(key, "<null>");
         return;
      }
      r.kv(key + ".status", result.getStatus());
      Throwable t = result.getThrowable();
      if (t == null)
      {
         r.kv(key + ".throwable", "<null>");
      }
      else if (fullMessages)
      {
         r.thrown(key + ".throwable", t);
      }
      else
      {
         r.thrownTypes(key + ".throwable", t);
      }
   }

   private static void log(Rec r)
   {
      r.list("log", Log.lines());
   }

   /** The registration surface: what a rename would take away. */
   private static void shape(Rec r, String key, Class<?> c)
   {
      List<String> ifaces = new ArrayList<String>();
      for (Class<?> i : c.getInterfaces())
      {
         ifaces.add(i.getName());
      }
      Collections.sort(ifaces);
      r.list(key + ".interfaces", ifaces);
      List<String> methods = new ArrayList<String>();
      for (Method m : c.getDeclaredMethods())
      {
         // Declarations only -- see the note in WebrunnerFixture.shape.
         if (m.isSynthetic())
         {
            continue;
         }
         StringBuilder sb = new StringBuilder(Modifier.toString(m.getModifiers()))
               .append(' ').append(m.getReturnType().getName())
               .append(' ').append(m.getName()).append('(');
         Class<?>[] ps = m.getParameterTypes();
         for (int i = 0; i < ps.length; i++)
         {
            sb.append(i == 0 ? "" : ",").append(ps[i].getName());
         }
         methods.add(sb.append(')').toString());
      }
      Collections.sort(methods);
      r.list(key + ".methods", methods);
   }
}
