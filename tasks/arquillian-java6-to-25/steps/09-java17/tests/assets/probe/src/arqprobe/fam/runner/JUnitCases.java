package arqprobe.fam.runner;

import org.jboss.arquillian.junit.Arquillian;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Assume;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * The test classes the JUnit integration is asked to run.
 *
 * These are ordinary Arquillian test classes: {@code @RunWith(Arquillian.class)}
 * and JUnit annotations, written the way the project's own documentation tells a
 * user to write them. That is the point — {@code JUnitTestRunner.execute} takes
 * a {@code Class} and a method name and runs it through JUnit, so the only way
 * to ask it a question is to hand it a class JUnit can run.
 *
 * <p>Each class is deliberately tiny and does exactly one thing, because the
 * observable is a single {@code TestResult}: a class with two interesting
 * methods would collapse two answers into one status. Bodies write to
 * {@link Log} rather than asserting, so a transcript shows what ran even when
 * the result says only "failed".
 *
 * <p><b>Why these are nested classes.</b> A runner's view of a class is its
 * name, its annotations and its methods; nesting changes none of those, and it
 * keeps the twenty-odd shapes this family needs in one file where the
 * differences between them are readable side by side. {@code Request.method}
 * accepts a nested class exactly as it accepts a top-level one — the servlet
 * family already records that the {@code $} in the name reaches the wire.
 */
public final class JUnitCases
{
   private JUnitCases()
   {
   }

   /** One passing method. The floor every other case is read against. */
   @RunWith(Arquillian.class)
   public static class Pass
   {
      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /** Two methods, so the transcript can show which one the name selected. */
   @RunWith(Arquillian.class)
   public static class TwoMethods
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
   @RunWith(Arquillian.class)
   public static class Throws
   {
      @Test
      public void shouldThrow()
      {
         Log.add("test:shouldThrow");
         throw new Boom("the test body said no");
      }
   }

   /** A body that throws an {@code Error} rather than an {@code Exception}. */
   @RunWith(Arquillian.class)
   public static class ThrowsError
   {
      @Test
      public void shouldThrow()
      {
         Log.add("test:shouldThrow");
         throw new Boom.Hard("the test body failed hard");
      }
   }

   /**
    * {@code @Test(expected)} satisfied.
    *
    * JUnit calls this a pass. The integration disagrees enough to carry the
    * exception back anyway — see the {@code @HACK} comment on
    * {@code Arquillian.caughtTestException} — so the interesting observable is
    * whether a PASSED result arrives holding a throwable.
    */
   @RunWith(Arquillian.class)
   public static class ExpectedThrown
   {
      @Test(expected = Boom.class)
      public void shouldThrowExpected()
      {
         Log.add("test:shouldThrowExpected");
         throw new Boom("expected by the annotation");
      }
   }

   /** {@code @Test(expected)} unsatisfied: JUnit fails it. */
   @RunWith(Arquillian.class)
   public static class ExpectedNotThrown
   {
      @Test(expected = Boom.class)
      public void shouldNotThrow()
      {
         Log.add("test:shouldNotThrow");
      }
   }

   /** {@code @Ignore}d, so JUnit reports an ignore rather than a pass. */
   @RunWith(Arquillian.class)
   public static class Ignored
   {
      @Test
      @Ignore("not today")
      public void shouldBeIgnored()
      {
         Log.add("test:shouldBeIgnored");
      }
   }

   /**
    * An assumption that does not hold.
    *
    * JUnit neither fails nor ignores this: it stops the method and reports a run
    * with no failures and no ignores. What the integration makes of that is the
    * question, and the answer is the same shape as a pass.
    */
   @RunWith(Arquillian.class)
   public static class AssumptionFails
   {
      @Test
      public void shouldBeAssumedAway()
      {
         Log.add("test:before-assume");
         Assume.assumeTrue(false);
         Log.add("test:after-assume");
      }
   }

   /** Every lifecycle hook, so the transcript shows the interleaving. */
   @RunWith(Arquillian.class)
   public static class Lifecycle
   {
      @BeforeClass
      public static void beforeClass()
      {
         Log.add("hook:@BeforeClass");
      }

      @AfterClass
      public static void afterClass()
      {
         Log.add("hook:@AfterClass");
      }

      @Before
      public void before()
      {
         Log.add("hook:@Before");
      }

      @After
      public void after()
      {
         Log.add("hook:@After");
      }

      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /** {@code @Before} throws: does the test body run, and does {@code @After}? */
   @RunWith(Arquillian.class)
   public static class BeforeThrows
   {
      @Before
      public void before()
      {
         Log.add("hook:@Before");
         throw new Boom("setup said no");
      }

      @After
      public void after()
      {
         Log.add("hook:@After");
      }

      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /** The body passes and {@code @After} throws. */
   @RunWith(Arquillian.class)
   public static class AfterThrows
   {
      @After
      public void after()
      {
         Log.add("hook:@After");
         throw new Boom("teardown said no");
      }

      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /**
    * The body throws an {@code Error} and there is an {@code @After} to run.
    *
    * The pair to {@link BothThrow}. {@code Arquillian.multiExecute} catches
    * {@code Exception}, so an {@code Error} from the body is not collected — it
    * leaves before the statement that would fire the Arquillian {@code after}
    * callback is ever evaluated. Whether JUnit's own {@code @After} still runs
    * is then a question about where the integration put it, and the transcript
    * answers it.
    */
   @RunWith(Arquillian.class)
   public static class ErrorWithAfter
   {
      @After
      public void after()
      {
         Log.add("hook:@After");
      }

      @Test
      public void shouldThrow()
      {
         Log.add("test:shouldThrow");
         throw new Boom.Hard("the test body failed hard");
      }
   }

   /** Both the body and {@code @After} throw, so two failures compete. */
   @RunWith(Arquillian.class)
   public static class BothThrow
   {
      @After
      public void after()
      {
         Log.add("hook:@After");
         throw new Boom("teardown said no");
      }

      @Test
      public void shouldThrow()
      {
         Log.add("test:shouldThrow");
         throw new Boom("the test body said no");
      }
   }

   /**
    * A test method that takes an argument.
    *
    * {@code Arquillian} overrides {@code validatePublicVoidNoArgMethods} so JUnit
    * accepts the class, which leaves the question of what invokes it with what.
    */
   @RunWith(Arquillian.class)
   public static class TakesArgument
   {
      @Test
      public void shouldTakeAnArgument(String argument)
      {
         Log.add("test:shouldTakeAnArgument arg=" + argument);
      }
   }

   /** No {@code @RunWith} at all: plain JUnit, no integration in the path. */
   public static class Plain
   {
      @Test
      public void shouldPass()
      {
         Log.add("test:shouldPass");
      }
   }

   /** A class JUnit itself rejects: no runnable methods. */
   @RunWith(Arquillian.class)
   public static class NoTestMethods
   {
      public void notATest()
      {
         Log.add("test:notATest");
      }
   }
}
