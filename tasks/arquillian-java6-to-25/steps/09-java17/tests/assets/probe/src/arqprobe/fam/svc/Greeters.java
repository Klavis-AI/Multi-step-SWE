package arqprobe.fam.svc;

import java.lang.reflect.Method;

import org.jboss.arquillian.spi.TestEnricher;
import org.jboss.arquillian.spi.TestResult;
import org.jboss.arquillian.spi.TestRunner;

/**
 * Provider shapes for the {@code svc} family.
 *
 * Both loaders find a provider by name and then have to construct it; one class
 * per way that construction can go.
 */
public final class Greeters
{
   /**
    * {@link Hidden} is not visible outside this package, so a fixture that wants
    * it declared names it the way a service file does.
    */
   public static final String HIDDEN = "arqprobe.fam.svc.Greeters$Hidden";

   private Greeters()
   {
   }

   public static class Alpha implements Greeter
   {
      public String greet()
      {
         return "alpha";
      }
   }

   public static class Beta implements Greeter
   {
      public String greet()
      {
         return "beta";
      }
   }

   /**
    * Out of reach: both loaders ask for the constructor with
    * {@code Class.getConstructor()}, which only sees public ones, so neither
    * gets far enough to call {@code setAccessible}.
    */
   public static class PrivateCtor implements Greeter
   {
      private PrivateCtor()
      {
      }

      public String greet()
      {
         return "private";
      }
   }

   /**
    * Package-private class with a public constructor: the case the loaders'
    * {@code setAccessible(true)} is actually load-bearing for, and the one a
    * module system can take away.
    */
   static class Hidden implements Greeter
   {
      public Hidden()
      {
      }

      public String greet()
      {
         return "hidden";
      }
   }

   public static class NoNoArgCtor implements Greeter
   {
      public NoNoArgCtor(String required)
      {
      }

      public String greet()
      {
         return "no-no-arg";
      }
   }

   public static class ThrowingCtor implements Greeter
   {
      public ThrowingCtor()
      {
         throw new IllegalStateException("provider refused to start");
      }

      public String greet()
      {
         return "unreachable";
      }
   }

   public abstract static class AbstractGreeter implements Greeter
   {
      public AbstractGreeter()
      {
      }
   }

   /** Named in a service file that declares a different service type. */
   public static class NotAGreeter
   {
      public NotAGreeter()
      {
      }
   }

   public static class RunnerOne implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         return new TestResult(TestResult.Status.PASSED);
      }
   }

   public static class RunnerTwo implements TestRunner
   {
      public TestResult execute(Class<?> testClass, String methodName)
      {
         return new TestResult(TestResult.Status.SKIPPED);
      }
   }

   /** Fills every argument slot, so a second enricher has nothing left to claim. */
   public static class FirstEnricher implements TestEnricher
   {
      public void enrich(Object testCase)
      {
      }

      public Object[] resolve(Method method)
      {
         Object[] values = new Object[method.getParameterTypes().length];
         for (int i = 0; i < values.length; i++)
         {
            values[i] = "first-" + i;
         }
         return values;
      }
   }

   public static class SecondEnricher implements TestEnricher
   {
      public void enrich(Object testCase)
      {
      }

      public Object[] resolve(Method method)
      {
         Object[] values = new Object[method.getParameterTypes().length];
         for (int i = 0; i < values.length; i++)
         {
            values[i] = "second-" + i;
         }
         return values;
      }
   }

   public static class EmptyEnricher implements TestEnricher
   {
      public void enrich(Object testCase)
      {
      }

      public Object[] resolve(Method method)
      {
         return new Object[0];
      }
   }

   public static class NullEnricher implements TestEnricher
   {
      public void enrich(Object testCase)
      {
      }

      public Object[] resolve(Method method)
      {
         return null;
      }
   }

   /** Returns the wrong number of slots, which the merge rejects. */
   public static class WrongArityEnricher implements TestEnricher
   {
      public void enrich(Object testCase)
      {
      }

      public Object[] resolve(Method method)
      {
         return new Object[method.getParameterTypes().length + 1];
      }
   }

   /** Target of {@code TestEnrichers.enrich}; its parameters are what gets resolved. */
   public static class EnrichmentTarget
   {
      public void noArguments()
      {
      }

      public void twoArguments(String first, Integer second)
      {
      }
   }
}
