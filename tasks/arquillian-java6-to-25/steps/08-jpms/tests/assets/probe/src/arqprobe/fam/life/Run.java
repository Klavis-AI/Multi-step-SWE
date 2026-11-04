package arqprobe.fam.life;

import java.lang.reflect.Method;
import java.util.List;

import arqprobe.Rec;

import org.jboss.arquillian.impl.DeployableTestBuilder;
import org.jboss.arquillian.spi.ContainerProfile;
import org.jboss.arquillian.spi.LifecycleMethodExecutor;
import org.jboss.arquillian.spi.TestResult;
import org.jboss.arquillian.spi.TestRunnerAdaptor;

/**
 * One client run, driven through the adaptor a framework integration uses.
 *
 * {@code TestRunnerAdaptor} is the whole of what a JUnit or TestNG integration
 * gets to call, so a fixture that calls it in a different order, or skips a
 * call, is asking a question a real integration could also ask. Every call is
 * recorded before it is made and its outcome after, into the same log the
 * container adapter and the tracing extension write to, so one sequence comes
 * out at the end.
 *
 * Nothing here throws: a phase that fails records how and the fixture decides
 * whether to keep going. That is the point — a run that dies at {@code start}
 * still has to unwind, and what it does on the way out is most of what these
 * fixtures are for.
 */
public final class Run
{
   private TestRunnerAdaptor adaptor;

   private TestResult result;

   /** Builds the manager for the client profile. */
   public void build()
   {
      step("build", new Body()
      {
         public void run() throws Throwable
         {
            adaptor = DeployableTestBuilder.build(ContainerProfile.CLIENT);
         }
      });
   }

   public void beforeSuite()
   {
      step("beforeSuite", new Body()
      {
         public void run() throws Throwable
         {
            adaptor.beforeSuite();
         }
      });
   }

   public void afterSuite()
   {
      step("afterSuite", new Body()
      {
         public void run() throws Throwable
         {
            adaptor.afterSuite();
         }
      });
   }

   public void beforeClass(final Class<?> testClass)
   {
      beforeClass(testClass, Tests.lifecycle("beforeClass:" + testClass.getSimpleName()));
   }

   public void beforeClass(final Class<?> testClass, final LifecycleMethodExecutor executor)
   {
      step("beforeClass:" + testClass.getSimpleName(), new Body()
      {
         public void run() throws Throwable
         {
            adaptor.beforeClass(testClass, executor);
         }
      });
   }

   public void afterClass(final Class<?> testClass)
   {
      afterClass(testClass, Tests.lifecycle("afterClass:" + testClass.getSimpleName()));
   }

   public void afterClass(final Class<?> testClass, final LifecycleMethodExecutor executor)
   {
      step("afterClass:" + testClass.getSimpleName(), new Body()
      {
         public void run() throws Throwable
         {
            adaptor.afterClass(testClass, executor);
         }
      });
   }

   public void before(final Object instance, final String methodName)
   {
      before(instance, methodName, Tests.lifecycle("before:" + methodName));
   }

   public void before(final Object instance, final String methodName,
         final LifecycleMethodExecutor executor)
   {
      step("before:" + methodName, new Body()
      {
         public void run() throws Throwable
         {
            adaptor.before(instance, lookup(instance, methodName), executor);
         }
      });
   }

   public void after(final Object instance, final String methodName)
   {
      after(instance, methodName, Tests.lifecycle("after:" + methodName));
   }

   public void after(final Object instance, final String methodName,
         final LifecycleMethodExecutor executor)
   {
      step("after:" + methodName, new Body()
      {
         public void run() throws Throwable
         {
            adaptor.after(instance, lookup(instance, methodName), executor);
         }
      });
   }

   /**
    * Runs one test method and records the result the adaptor hands back.
    *
    * Only the status and the throwable are recorded. {@code TestResult} also
    * carries a start and an end taken from {@code System.currentTimeMillis},
    * which no recording may contain.
    */
   public void test(final Object instance, final String methodName)
   {
      result = null;
      step("test:" + methodName, new Body()
      {
         public void run() throws Throwable
         {
            result = adaptor.test(new Tests.Exec(instance, methodName));
         }
      });
      if (result == null)
      {
         Log.add("result:<none>");
         return;
      }
      Log.add("result:status=" + result.getStatus()
            + " throwable=" + chain(result.getThrowable()));
   }

   /** The whole of one method: before, test, after. */
   public void method(Object instance, String methodName)
   {
      before(instance, methodName);
      test(instance, methodName);
      after(instance, methodName);
   }

   public void shutdown()
   {
      step("shutdown", new Body()
      {
         public void run() throws Throwable
         {
            adaptor.shutdown();
         }
      });
   }

   /** The recorded sequence, oldest first. */
   public List<String> lines()
   {
      return Log.lines();
   }

   // --- plumbing ------------------------------------------------------------

   private interface Body
   {
      void run() throws Throwable;
   }

   private void step(String label, Body body)
   {
      if (adaptor == null && !"build".equals(label))
      {
         Log.add("phase:" + label + " skipped (no adaptor)");
         return;
      }
      Log.add("phase:" + label);
      try
      {
         body.run();
         Log.add("phase:" + label + " ok");
      }
      catch (Throwable t)
      {
         Log.add("phase:" + label + " threw " + chain(t));
      }
   }

   private static Method lookup(Object instance, String name)
   {
      return new Tests.Exec(instance, name).getMethod();
   }

   /** A throwable as the chain of types and messages the recorder would keep. */
   static String chain(Throwable t)
   {
      if (t == null)
      {
         return "<none>";
      }
      StringBuilder sb = new StringBuilder();
      for (String[] link : Rec.chainOf(t))
      {
         if (sb.length() > 0)
         {
            sb.append(" <- ");
         }
         sb.append(link[0]).append(": ").append(link[1]);
      }
      return sb.toString();
   }
}
