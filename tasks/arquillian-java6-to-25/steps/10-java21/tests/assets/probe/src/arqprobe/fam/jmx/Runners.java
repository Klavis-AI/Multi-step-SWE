package arqprobe.fam.jmx;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.reflect.InvocationTargetException;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.ArrayList;
import java.util.List;

import javax.management.MBeanServer;
import javax.management.MBeanServerFactory;

import org.jboss.arquillian.protocol.jmx.JMXTestRunner;
import org.jboss.arquillian.protocol.jmx.JMXTestRunnerMBean;
import org.jboss.arquillian.spi.TestResult;
import org.jboss.arquillian.spi.TestResult.Status;
import org.jboss.arquillian.spi.TestRunner;

/**
 * The two pieces of scaffolding the JMX runner needs before it can be asked
 * anything: somewhere for its {@code TestRunner} to come from, and somewhere to
 * put the {@code MBeanServer}s it leaves behind.
 *
 * <h3>Why a class loader and not a service file</h3>
 *
 * {@code JMXTestRunner.runTestMethodInternal} asks for its runner with
 * {@code TestRunners.getTestRunner(JMXTestRunner.class.getClassLoader())} --
 * not the thread context loader, not one it was handed. On the scored class
 * path that is the application loader, and no jar in this tree registers a
 * {@code TestRunner} there, so the shipped behaviour is a {@code FAILED} result
 * carrying {@code NoSuchElementException}. That is worth recording, and it is.
 *
 * <p>But it is not the only behaviour worth recording, and the obvious way to
 * reach the rest -- dropping a service file into the harness's own
 * {@code META-INF/services} -- would put a {@code TestRunner} on the
 * application loader for every fixture in every family, and take the shipped
 * behaviour away at the same time. So the registration is made visible to
 * {@code JMXTestRunner} alone, by loading {@code JMXTestRunner} itself in a
 * loader that has the service file: {@link #isolated}. Everything else, the
 * whole SPI included, still comes from the parent, so a {@code TestResult} the
 * isolated runner produces is the same class the fixture compares against.
 *
 * <p>Child-first is restricted to {@code JMXTestRunner} and its anonymous inner
 * class, and deliberately excludes {@code JMXTestRunner$TestClassLoader}: the
 * fixture passes an implementation of that interface into the constructor, and
 * two copies of it would not be assignable to each other.
 *
 * <h3>Why the MBeanServer bookkeeping</h3>
 *
 * {@code JMXMethodExecutor.findOrCreateMBeanServer} creates a server through
 * {@code MBeanServerFactory} and never releases it, and that factory's registry
 * is global to the JVM. One fixture's leak is the next fixture's
 * "{@code findMBeanServer} returned one already". {@link #snapshot} and
 * {@link #releaseNew} bracket every fixture so each one starts from whatever
 * the JVM had before the family ran -- releasing only servers the fixture
 * caused to exist, never a platform server that was already there.
 */
public final class Runners
{
   /** The one class the isolated loader defines itself. */
   private static final String TARGET = JMXTestRunner.class.getName();

   private static final String SERVICE = "META-INF/services/" + TestRunner.class.getName();

   private Runners()
   {
   }

   // ------------------------------------------------------------------------
   // the isolated runner
   // ------------------------------------------------------------------------

   /**
    * A loader that defines {@code JMXTestRunner} itself and answers its service
    * lookup with the given file contents.
    *
    * @param serviceFile the bytes of {@code META-INF/services/…TestRunner}, or
    *                    null for a loader that declares no runner at all
    */
   public static ClassLoader isolated(String serviceFile) throws IOException
   {
      File root = scratch();
      if (serviceFile != null)
      {
         write(new File(root, SERVICE), serviceFile);
      }
      URL jar = JMXTestRunner.class.getProtectionDomain().getCodeSource().getLocation();
      return new Isolating(new URL[] {root.toURI().toURL(), jar}, Runners.class.getClassLoader());
   }

   /** {@link #isolated} declaring one provider per line, the ordinary case. */
   public static ClassLoader isolated(Class<?>... providers) throws IOException
   {
      StringBuilder sb = new StringBuilder();
      for (int i = 0; i < providers.length; i++)
      {
         sb.append(providers[i].getName()).append('\n');
      }
      return isolated(sb.toString());
   }

   /**
    * Construct a {@code JMXTestRunner} in the given loader.
    *
    * Reflective because the class is not the one this file was compiled
    * against; the interface it is returned as is, which is the whole point of
    * keeping {@code JMXTestRunnerMBean} on the parent.
    */
   public static JMXTestRunnerMBean runner(ClassLoader loader, Object testClassLoader)
         throws Exception
   {
      Class<?> c = loader.loadClass(TARGET);
      return (JMXTestRunnerMBean) c.getConstructor(JMXTestRunner.TestClassLoader.class)
            .newInstance(testClassLoader);
   }

   /**
    * {@code registerMBean} and {@code unregisterMBean} are on the class, not on
    * the MBean interface, so an isolated runner can only be asked reflectively.
    *
    * The cause is unwrapped rather than passed on: a fixture asking what
    * registering twice does wants {@code InstanceAlreadyExistsException}, not
    * {@code InvocationTargetException} wrapping it, and the answer must not
    * depend on how the fixture happened to reach the method.
    */
   public static Object register(Object runner, MBeanServer server) throws Throwable
   {
      return call(runner, "registerMBean", server);
   }

   public static Object unregister(Object runner, MBeanServer server) throws Throwable
   {
      return call(runner, "unregisterMBean", server);
   }

   private static Object call(Object runner, String method, MBeanServer server) throws Throwable
   {
      try
      {
         return runner.getClass().getMethod(method, MBeanServer.class).invoke(runner, server);
      }
      catch (InvocationTargetException e)
      {
         throw e.getCause();
      }
   }

   /** Which loader actually defined the runner, so a fixture can prove the isolation took. */
   public static String loaderOf(Object runner)
   {
      ClassLoader cl = runner.getClass().getClassLoader();
      return cl == Runners.class.getClassLoader() ? "<parent>" : "<isolated>";
   }

   private static final class Isolating extends URLClassLoader
   {
      Isolating(URL[] urls, ClassLoader parent)
      {
         super(urls, parent);
      }

      @Override
      protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException
      {
         if (!childFirst(name))
         {
            return super.loadClass(name, resolve);
         }
         synchronized (getClassLoadingLock(name))
         {
            Class<?> c = findLoadedClass(name);
            if (c == null)
            {
               c = findClass(name);
            }
            if (resolve)
            {
               resolveClass(c);
            }
            return c;
         }
      }

      private static boolean childFirst(String name)
      {
         return name.equals(TARGET)
               || (name.startsWith(TARGET + "$") && !name.equals(TARGET + "$TestClassLoader"));
      }
   }

   // ------------------------------------------------------------------------
   // the runners themselves
   // ------------------------------------------------------------------------

   /** Records what it was asked and reports a pass. */
   public static final class Passing implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + (testClass == null ? "<null>" : testClass.getName()) + "#" + methodName);
         return new TestResult(Status.PASSED);
      }
   }

   /** A second passing runner, for the two-registrations case. */
   public static final class AlsoPassing implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("also-execute " + testClass.getName() + "#" + methodName);
         return new TestResult(Status.PASSED);
      }
   }

   /** Reports a failure the way a test framework would: a result, not a throw. */
   public static final class Failing implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         return new TestResult(Status.FAILED, new AssertionError("expected 1 but was 2"));
      }
   }

   public static final class Skipping implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         return new TestResult(Status.SKIPPED);
      }
   }

   /** Does not come back at all, which the runner has to turn into a result. */
   public static final class Throwing implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         throw new IllegalStateException("the test framework could not start");
      }
   }

   /** Hands back nothing, which nothing downstream checks for. */
   public static final class Nulling implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         return null;
      }
   }

   /** A failure whose throwable holds a field that cannot be serialized. */
   public static final class Unmarshallable implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         return new TestResult(Status.FAILED, new Awkward("the result cannot be marshalled"));
      }
   }

   /** Serializable by inheritance, not in fact: the field is an ordinary Object. */
   public static final class Awkward extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      @SuppressWarnings("unused")
      private final Object payload = new Object();

      public Awkward(String message)
      {
         super(message);
      }
   }

   /** Fails with a throwable the client cannot rebuild, to exercise the proxy's fallback. */
   public static final class Stubborning implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         return new TestResult(Status.FAILED, new Stubborn(7));
      }
   }

   /**
    * A throwable {@code ExceptionProxy} cannot reconstruct.
    *
    * The proxy tries {@code (String, Throwable)}, then {@code (String,
    * Exception)}, then {@code (String)}, and gives up. This class offers none
    * of the three, so the client gets an {@code ArquillianProxyException}
    * quoting the original class, message and reason instead.
    */
   public static final class Stubborn extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      public Stubborn(int code)
      {
         super("cannot be rebuilt (" + code + ")");
      }
   }

   /** Named in a service file but not a {@code TestRunner}. */
   public static final class NotARunner
   {
   }

   /** A {@code TestRunner} with no no-argument constructor. */
   public static final class NeedsAnArgument implements TestRunner
   {
      public NeedsAnArgument(String unused)
      {
      }

      public TestResult execute(Class<?> testClass, String methodName)
      {
         return new TestResult(Status.PASSED);
      }
   }

   // ------------------------------------------------------------------------
   // test class loaders
   // ------------------------------------------------------------------------

   /** Records the name it was asked for and answers with the harness's own loader. */
   public static final class Recording implements JMXTestRunner.TestClassLoader
   {
      public Class<?> loadTestClass(String className) throws ClassNotFoundException
      {
         Log.add("loadTestClass " + className);
         return Runners.class.getClassLoader().loadClass(className);
      }
   }

   /** Refuses everything, the way a container whose deployment is gone would. */
   public static final class Refusing implements JMXTestRunner.TestClassLoader
   {
      public Class<?> loadTestClass(String className) throws ClassNotFoundException
      {
         Log.add("loadTestClass " + className);
         throw new ClassNotFoundException("no deployment holds " + className);
      }
   }

   /** Answers null, which nothing between here and the runner checks. */
   public static final class Nothing implements JMXTestRunner.TestClassLoader
   {
      public Class<?> loadTestClass(String className)
      {
         Log.add("loadTestClass " + className);
         return null;
      }
   }

   // ------------------------------------------------------------------------
   // MBeanServer bookkeeping
   // ------------------------------------------------------------------------

   /** The servers the factory knows about right now. */
   public static List<MBeanServer> snapshot()
   {
      return new ArrayList<MBeanServer>(MBeanServerFactory.findMBeanServer(null));
   }

   /**
    * Release every server that has appeared since the snapshot.
    *
    * Only those: a platform server that was already registered belongs to
    * whatever registered it, and releasing it would break the next fixture in a
    * way that looked like a codebase change.
    */
   public static void releaseNew(List<MBeanServer> before)
   {
      for (MBeanServer server : snapshot())
      {
         if (!contains(before, server))
         {
            MBeanServerFactory.releaseMBeanServer(server);
         }
      }
   }

   /** How many servers exist beyond the ones in the snapshot. */
   public static int createdSince(List<MBeanServer> before)
   {
      int n = 0;
      for (MBeanServer server : snapshot())
      {
         if (!contains(before, server))
         {
            n++;
         }
      }
      return n;
   }

   private static boolean contains(List<MBeanServer> servers, MBeanServer server)
   {
      for (MBeanServer s : servers)
      {
         if (s == server)
         {
            return true;
         }
      }
      return false;
   }

   // ------------------------------------------------------------------------

   private static File scratch() throws IOException
   {
      File dir = File.createTempFile("arqprobe-jmx", "");
      if (!dir.delete() || !dir.mkdirs())
      {
         throw new IOException("could not create a scratch directory");
      }
      return dir;
   }

   private static void write(File file, String contents) throws IOException
   {
      if (!file.getParentFile().mkdirs())
      {
         throw new IOException("could not create " + file.getParentFile());
      }
      OutputStream out = new FileOutputStream(file);
      try
      {
         out.write(contents.getBytes("UTF-8"));
      }
      finally
      {
         out.close();
      }
   }
}
