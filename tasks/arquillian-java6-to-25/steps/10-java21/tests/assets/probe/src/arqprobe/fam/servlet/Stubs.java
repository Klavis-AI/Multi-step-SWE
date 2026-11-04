package arqprobe.fam.servlet;

import java.lang.reflect.Method;

import org.jboss.arquillian.spi.TestDeployment;
import org.jboss.arquillian.spi.TestMethodExecutor;
import org.jboss.arquillian.spi.client.deployment.ProtocolArchiveProcessor;
import org.jboss.shrinkwrap.api.Archive;

/**
 * The pluggable pieces the protocol is handed.
 *
 * Nothing here implements a renamed API: a {@code ProtocolArchiveProcessor} and
 * a {@code TestMethodExecutor} are this codebase's own interfaces and are the
 * same on every rung of the ladder. The servlet API, which is not, is never
 * named in harness source — see {@link Http}.
 */
public final class Stubs
{
   private Stubs()
   {
   }

   /**
    * A registered archive processor that says what it was handed.
    *
    * The interface is named {@code ProtocolArchiveProcessor}, and the archive it
    * receives is usually not the protocol archive. The return value of {@code
    * generateDeployment} cannot show which archive it was, because on three of
    * the four shapes the thing handed over is nested inside, or replaced by, the
    * thing returned — so this records the identity of the archive it got
    * relative to the deployment it was given, and not just its name.
    *
    * <p>Measured, over the {@code *-processors} cases of the shared matrix:
    * <pre>
    *   shape             Servlet 2.5                  Servlet 3.0
    *   war               the application war           the application war
    *   ear + one war     the nested war                the nested war
    *   ear, no war       arquillian-protocol.war       a synthesized test.war
    *   jar               arquillian-protocol.war       a synthesized test.war
    * </pre>
    * Only the first row is the application archive the caller passed in; the
    * 3.0 packager never hands over its own protocol archive at all, because that
    * archive is a {@code .jar} library and the processor is always given the war
    * carrying it. Recording the boolean alone would merge the bottom three rows,
    * so the name is recorded with it, and the entry count with that — the two
    * {@code test.war}s of the 3.0 column hold different things.
    */
   public static final class Processor implements ProtocolArchiveProcessor
   {
      private final String label;

      private final RuntimeException failure;

      public Processor(String label)
      {
         this(label, null);
      }

      public Processor(String label, RuntimeException failure)
      {
         this.label = label;
         this.failure = failure;
      }

      public void process(TestDeployment testDeployment, Archive<?> protocolArchive)
      {
         Log.add("processor:" + label
               + " got=" + protocolArchive.getName()
               + " isApplication=" + (protocolArchive == testDeployment.getApplicationArchive())
               + " entries=" + Archives.entries(protocolArchive));
         if (failure != null)
         {
            throw failure;
         }
      }
   }

   /**
    * The test method the executor is asked to run remotely.
    *
    * {@code ServletMethodExecutor} reads two things off it — the instance's
    * class name and the method's name — and builds a query string out of them.
    * {@code invoke} is never called on the client side; the executor's whole job
    * is to make the call happen somewhere else.
    */
   public static final class MethodExecutor implements TestMethodExecutor
   {
      private final Object instance;

      private final Method method;

      public MethodExecutor(Object instance, Method method)
      {
         this.instance = instance;
         this.method = method;
      }

      public Method getMethod()
      {
         return method;
      }

      public Object getInstance()
      {
         return instance;
      }

      public void invoke(Object... parameters) throws Throwable
      {
         Log.add("executor:invoke-called-locally");
         throw new IllegalStateException("the client must not run the test itself");
      }
   }

   /** The test class a fixture asks to have run. Its name reaches the wire. */
   public static class Case
   {
      public void shouldPass()
      {
      }

      public void shouldFail()
      {
         throw new IllegalStateException("the test said no");
      }

      public void takesArguments(String a, int b)
      {
      }

      /** A nested class, so a {@code $} has to survive the query string. */
      public static class Nested
      {
         public void shouldPass()
         {
         }
      }
   }

   /**
    * A failure the result's exception proxy can rebuild exactly.
    *
    * {@code TestResult} declares its {@code throwable} field {@code transient}
    * and carries an {@code ExceptionProxy} instead, so nothing about a failure
    * crosses the wire as itself: the class name, the message and the stack trace
    * are shipped, and the client reconstructs the exception by reflection. This
    * class offers the {@code (String, Throwable)} constructor the proxy looks
    * for first, and so comes back as itself with its cause attached.
    */
   public static class Failure extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      public Failure(String message, Throwable cause)
      {
         super(message, cause);
      }
   }

   /**
    * A failure the proxy cannot rebuild.
    *
    * The proxy tries {@code (String, Throwable)}, then {@code (String, Exception)},
    * then {@code (String)}, and gives up. This class offers none of them, so the
    * client receives an {@code ArquillianProxyException} naming the original
    * instead — the substitution is the observable, and it is the kind of thing a
    * migration that "modernised" the constructor lookup would quietly change.
    */
   public static class Unconstructable extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      public Unconstructable(int code)
      {
         super("code " + code);
      }
   }
}
