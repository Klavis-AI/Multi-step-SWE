package arqprobe.fam.runner;

import org.jboss.arquillian.testng.Arquillian;
import org.testng.SkipException;
import org.testng.annotations.AfterClass;
import org.testng.annotations.AfterMethod;
import org.testng.annotations.BeforeClass;
import org.testng.annotations.BeforeMethod;
import org.testng.annotations.Test;

/**
 * The test classes the TestNG integration is asked to run.
 *
 * The TestNG integration is not a runner the way the JUnit one is: there is no
 * {@code @RunWith} to point at it, so a test class joins it by extending
 * {@link Arquillian}, and everything the integration does it does from the
 * lifecycle annotations on that base class. That difference is the reason this
 * file exists alongside {@link JUnitCases} rather than sharing it — the same
 * question ("does {@code @AfterMethod} still run when the body throws?") is
 * asked of a different mechanism on each side, and the two answers are worth
 * having next to each other.
 *
 * <p>The base class is abstract and every method on it is public, so a subclass
 * inherits {@code arquillianBeforeSuite}, {@code arquillianBeforeClass},
 * {@code arquillianBeforeTest} and their partners without naming them. TestNG
 * runs a superclass's {@code @BeforeClass} before a subclass's and a subclass's
 * {@code @AfterClass} before the superclass's, which is what puts the Arquillian
 * callbacks outside the fixture's own hooks; {@link Lifecycle} records that
 * nesting rather than assuming it.
 *
 * <p>Bodies write to {@link Log} rather than asserting, for the reason
 * {@link Boom} gives: an assertion failure carries a sentence TestNG composed
 * about its own machinery, and the step that moves TestNG 5.14.6 forward is free
 * to reword every one of them.
 */
public final class TestNGCases
{
   private TestNGCases()
   {
   }

   /** One passing method. The floor every other case is read against. */
   public static class Pass extends Arquillian
   {
      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /** Two methods, so the transcript can show which one the name selected. */
   public static class TwoMethods extends Arquillian
   {
      @Test
      public void first()
      {
         Log.add("test:first");
      }

      @Test
      public void second()
      {
         Log.add("test:second");
      }
   }

   /** A body that throws a throwable this suite owns. */
   public static class Throws extends Arquillian
   {
      @Test
      public void shouldThrow()
      {
         Log.add("test:shouldThrow");
         throw new Boom("the test body said no");
      }
   }

   /**
    * A body that throws an {@code Error}.
    *
    * {@code Arquillian.run} catches {@code Exception}, and the executor it hands
    * the manager calls {@code IHookCallBack.runTestMethod}, which is TestNG's
    * own invoker and does its own catching. Which of the two sees an
    * {@code Error} first decides whether the failure arrives as the throwable on
    * the {@code TestResult} or as the one TestNG recorded for itself, and the
    * transcript shows which by whether a throwable came back at all.
    */
   public static class ThrowsError extends Arquillian
   {
      @Test
      public void shouldThrow()
      {
         Log.add("test:shouldThrow");
         throw new Boom.Hard("the test body failed hard");
      }
   }

   /** {@code expectedExceptions} satisfied: TestNG calls this a pass. */
   public static class ExpectedThrown extends Arquillian
   {
      @Test(expectedExceptions = Boom.class)
      public void shouldThrowExpected()
      {
         Log.add("test:shouldThrowExpected");
         throw new Boom("expected by the annotation");
      }
   }

   /**
    * A body that skips itself.
    *
    * {@code SkipException} is TestNG's own; there is no JUnit equivalent, and
    * {@code TestListener.getTestResult} has a whole branch for it that no other
    * case reaches. The branch returns {@code new TestResult(Status.SKIPPED)} —
    * the one-argument constructor — so the interesting observable is whether a
    * skipped run carries the exception that caused it.
    */
   public static class Skipped extends Arquillian
   {
      @Test
      public void shouldSkip()
      {
         Log.add("test:shouldSkip");
         throw new SkipException("skipped by the body");
      }
   }

   /** Every lifecycle hook, so the transcript shows the nesting. */
   public static class Lifecycle extends Arquillian
   {
      @BeforeClass
      public void beforeClass()
      {
         Log.add("hook:@BeforeClass");
      }

      @AfterClass
      public void afterClass()
      {
         Log.add("hook:@AfterClass");
      }

      @BeforeMethod
      public void beforeMethod()
      {
         Log.add("hook:@BeforeMethod");
      }

      @AfterMethod
      public void afterMethod()
      {
         Log.add("hook:@AfterMethod");
      }

      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /** {@code @BeforeMethod} throws: does the body run, and does the teardown? */
   public static class BeforeThrows extends Arquillian
   {
      @BeforeMethod
      public void beforeMethod()
      {
         Log.add("hook:@BeforeMethod");
         throw new Boom("setup said no");
      }

      @AfterMethod
      public void afterMethod()
      {
         Log.add("hook:@AfterMethod");
      }

      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /** The body passes and {@code @AfterMethod} throws. */
   public static class AfterThrows extends Arquillian
   {
      @AfterMethod
      public void afterMethod()
      {
         Log.add("hook:@AfterMethod");
         throw new Boom("teardown said no");
      }

      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /**
    * A method that depends on a method that does not exist.
    *
    * {@code TestNGTestRunner} registers {@code RemoveDependsOnTransformer},
    * whose whole job is to empty {@code dependsOnMethods} before TestNG resolves
    * it — because a container-side run is given one method to run and cannot
    * satisfy a dependency on a method the suite did not include. Naming a method
    * that is not there makes the transformer load-bearing: without it TestNG
    * refuses the class, with it the body runs, and the transcript says which
    * happened.
    */
   public static class DependsOnMethod extends Arquillian
   {
      @Test(dependsOnMethods = "noSuchMethod")
      public void shouldRunAnyway()
      {
         Log.add("test:shouldRunAnyway");
      }
   }

   /** The same question for the other line of the transformer. */
   public static class DependsOnGroup extends Arquillian
   {
      @Test(dependsOnGroups = "noSuchGroup")
      public void shouldRunAnyway()
      {
         Log.add("test:shouldRunAnyway");
      }
   }

   /**
    * A test method with a parameter and no data provider.
    *
    * {@code TestDataProviderTransformer} exists to attach one, and nothing
    * registers it (see the notes) — so this records what the shipped runner
    * actually does with an enrichable parameter, which is not what the unused
    * transformer would have made it do.
    */
   public static class TakesArgument extends Arquillian
   {
      @Test
      public void shouldTakeAnArgument(String argument)
      {
         Log.add("test:shouldTakeAnArgument arg=" + argument);
      }
   }

   /**
    * A parameter fed by the data provider the base class carries.
    *
    * {@code Arquillian.arquillianArgumentProvider} returns one row of nulls, and
    * {@code Arquillian.run} then copies whatever the manager resolved over that
    * row before invoking, and replaces the row with {@code toString}s afterwards
    * so TestNG's report writer never touches a contextual object. The body sees
    * the first half; {@code swapWithClassNames} is the second and runs after it.
    */
   public static class DataProvider extends Arquillian
   {
      @Test(dataProvider = Arquillian.ARQUILLIAN_DATA_PROVIDER)
      public void shouldTakeAnArgument(String argument)
      {
         Log.add("test:shouldTakeAnArgument arg=" + argument);
      }
   }

   /**
    * A TestNG class that does not extend {@link Arquillian}.
    *
    * Nothing sets the adaptor, so none of the integration's callbacks exist on
    * the class and TestNG runs it as an ordinary test. The counterpart to
    * {@code JUnitCases.Plain}.
    */
   public static class NoAdaptor
   {
      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /** Extends the base class but has no {@code @Test} of its own. */
   public static class NoTestMethods extends Arquillian
   {
      public void notATest()
      {
         Log.add("test:notATest");
      }
   }
}
