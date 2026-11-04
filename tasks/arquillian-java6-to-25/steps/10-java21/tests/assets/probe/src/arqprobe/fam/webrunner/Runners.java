package arqprobe.fam.webrunner;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.URL;
import java.net.URLClassLoader;

import org.jboss.arquillian.spi.TestResult;
import org.jboss.arquillian.spi.TestResult.Status;
import org.jboss.arquillian.spi.TestRunner;

/**
 * Where the servlet's {@code TestRunner} comes from, and what it does when it
 * gets there.
 *
 * <h3>Why the thread context loader, and not an isolating one</h3>
 *
 * {@code ServletTestRunner.doGet} resolves its runner with the <em>no-argument</em>
 * {@code TestRunners.getTestRunner()}, which delegates to
 * {@code SecurityActions.getThreadContextClassLoader()}. It loads the test class
 * through that same loader. So both halves of the lookup are controlled by one
 * thing the fixture already owns -- the thread's context class loader -- and the
 * child-first trickery the {@code jmx} family needs is unnecessary here. A plain
 * {@link URLClassLoader} over a scratch directory holding one service file, with
 * the application loader as its parent, is enough.
 *
 * <p>That choice is also what keeps the shipped behaviour recordable. No jar in
 * this tree registers a {@code TestRunner} on the application class path -- it
 * was checked, the only {@code META-INF/services} entries are for
 * {@code Protocol}, {@code TestEnricher}, {@code AuxiliaryArchiveAppender},
 * {@code ProtocolArchiveProcessor}, {@code DeploymentPackager} and
 * {@code ArquillianDescriptor} -- so a fixture that leaves the context loader
 * alone sees exactly what a deployment missing its runner registration sees, and
 * {@code webrunner/shipped-html} records it.
 */
public final class Runners
{
   private static final String SERVICE = "META-INF/services/" + TestRunner.class.getName();

   private Runners()
   {
   }

   // ------------------------------------------------------------------------
   // the loader the servlet is given
   // ------------------------------------------------------------------------

   /**
    * A loader that answers the {@code TestRunner} service lookup with the given
    * file contents, and delegates everything else to the application loader.
    *
    * @param serviceFile the bytes of {@code META-INF/services/…TestRunner}, or
    *                    null for a loader that declares no runner at all
    */
   public static ClassLoader providing(String serviceFile) throws IOException
   {
      File root = scratch();
      if (serviceFile != null)
      {
         write(new File(root, SERVICE), serviceFile);
      }
      return new URLClassLoader(new URL[] {root.toURI().toURL()}, Runners.class.getClassLoader());
   }

   /** {@link #providing} declaring one provider per line, the ordinary case. */
   public static ClassLoader providing(Class<?>... providers) throws IOException
   {
      StringBuilder sb = new StringBuilder();
      for (int i = 0; i < providers.length; i++)
      {
         sb.append(providers[i].getName()).append('\n');
      }
      return providing(sb.toString());
   }

   /** A loader that can find nothing at all, for proving a lookup used it. */
   public static ClassLoader empty() throws IOException
   {
      return new URLClassLoader(new URL[] {scratch().toURI().toURL()}, null);
   }

   // ------------------------------------------------------------------------
   // the runners themselves
   // ------------------------------------------------------------------------

   /** Reports what it was asked to run, then passes. */
   public static final class Recording implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + (testClass == null ? "<null>" : testClass.getName()) + "#" + methodName);
         return new TestResult(Status.PASSED);
      }
   }

   /** A second provider, so two names in one service file are two runners. */
   public static final class AlsoRecording implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("also execute " + testClass.getName() + "#" + methodName);
         return new TestResult(Status.PASSED);
      }
   }

   /** A failing test: the result travels, the throwable travels inside it. */
   public static final class Failing implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         return new TestResult(Status.FAILED, new AssertionError("expected 1 but was 2"));
      }
   }

   /** A failure the {@code ExceptionProxy} cannot rebuild on the far side. */
   public static final class Unmarshallable implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         return new TestResult(Status.FAILED, new Stubborn(7));
      }
   }

   /** No public {@code (String)}, {@code (String,Throwable)} or {@code (String,Exception)} constructor. */
   public static final class Stubborn extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      public Stubborn(int code)
      {
         super("cannot be rebuilt (" + code + ")");
      }
   }

   /** Throws instead of returning, which {@code doGet}'s catch turns into a result. */
   public static final class Throwing implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         throw new IllegalStateException("the test framework could not start");
      }
   }

   /**
    * Throws an {@code Error}. {@code doGet} catches {@code Exception}, so this
    * one leaves the servlet entirely and neither output mode is reached.
    */
   public static final class Erroring implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         throw new Boom("the deployment is past saving");
      }
   }

   public static final class Boom extends Error
   {
      private static final long serialVersionUID = 1L;

      public Boom(String message)
      {
         super(message);
      }
   }

   /** Returns null, which nothing on the way out checks. */
   public static final class Nulling implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         Log.add("execute " + testClass.getName() + "#" + methodName);
         return null;
      }
   }

   /** Not a {@code TestRunner} at all, for a service file that names the wrong thing. */
   public static final class NotARunner
   {
      public NotARunner()
      {
      }
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
   // scratch
   // ------------------------------------------------------------------------

   private static File scratch() throws IOException
   {
      File f = File.createTempFile("arqprobe-webrunner", "");
      if (!f.delete() || !f.mkdirs())
      {
         throw new IOException("could not make a scratch directory at " + f);
      }
      f.deleteOnExit();
      return f;
   }

   private static void write(File target, String contents) throws IOException
   {
      if (!target.getParentFile().mkdirs() && !target.getParentFile().isDirectory())
      {
         throw new IOException("could not make " + target.getParentFile());
      }
      OutputStream out = new FileOutputStream(target);
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
