package arqprobe.fam;

import java.io.File;
import java.lang.reflect.Method;
import java.util.Arrays;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.life.Env;
import arqprobe.fam.runner.Appenders;
import arqprobe.fam.runner.Events;
import arqprobe.fam.runner.JUnitCases;
import arqprobe.fam.runner.Log;
import arqprobe.fam.runner.Runners;
import arqprobe.fam.runner.TestNGCases;
import arqprobe.fam.runner.Transformers;

import org.jboss.arquillian.impl.DeployableTestBuilder;
import org.jboss.arquillian.junit.JUnitDeploymentAppender;
import org.jboss.arquillian.junit.JUnitTestRunner;
import org.jboss.arquillian.spi.ContainerProfile;
import org.jboss.arquillian.spi.Profile;
import org.jboss.arquillian.spi.TestResult;
import org.jboss.arquillian.spi.TestRunner;
import org.jboss.arquillian.testng.RemoveDependsOnTransformer;
import org.jboss.arquillian.testng.TestDataProviderTransformer;
import org.jboss.arquillian.testng.TestEnricherDataProvider;
import org.jboss.arquillian.testng.TestListener;
import org.jboss.arquillian.testng.TestNGDeploymentAppender;
import org.jboss.arquillian.testng.TestNGTestRunner;
import org.testng.TestNG;
import org.testng.annotations.ITestAnnotation;
import org.testng.xml.XmlClass;
import org.testng.xml.XmlInclude;
import org.testng.xml.XmlSuite;
import org.testng.xml.XmlTest;

/**
 * The two test-framework integrations, driven through the SPI that names them.
 *
 * {@code TestRunner} is a one-method interface — {@code TestResult
 * execute(Class<?>, String)} — and it is the whole of what Arquillian asks a
 * framework integration to do. Both integrations register themselves under it
 * in {@code META-INF/services}, and the servlet runner that ships into the
 * container looks them up through it. So a fixture that calls {@code execute}
 * is standing exactly where the shipped code stands.
 *
 * <p><b>What that pulls in behind it.</b> {@code execute} sets the CONTAINER
 * profile and runs the class through the real framework, which constructs the
 * integration's own runner — {@code org.jboss.arquillian.junit.Arquillian}, a
 * JUnit {@code Runner}, or {@code org.jboss.arquillian.testng.Arquillian}, a
 * TestNG {@code IHookable} base class — which in turn builds a real manager and
 * drives it through {@code TestRunnerAdaptor}. One call therefore exercises the
 * integration, the runner and the container-profile half of the manager
 * together, in the arrangement a deployed test actually runs in. The CONTAINER
 * profile registers no container and no deployment (see
 * {@code ArquillianProfile.getContainerProfile}), which is why this family needs
 * no {@code arquillian.xml} and no container adapter the way {@code life} does.
 *
 * <p><b>What is deliberately not recorded.</b> Anything the test framework
 * composed about itself. JUnit's {@code expected:<x> but was:<y>}, its
 * "no runnable methods" initialization error and TestNG's {@code toString} of a
 * test context are that framework's sentences about its own machinery, and the
 * step that moves JUnit 4.8.1 and TestNG 5.14.6 forward is free to reword every
 * one of them — the same trap as the JDK-composed messages in {@code Rec}, one
 * layer out. Where a fixture's question is "does this fail, and how", the type
 * chain answers it; where the throwable is one the fixture itself threw, it is
 * recorded in full, because that one is owned here.
 */
public class RunnerFixture implements Fixture
{
   private final String name;

   public RunnerFixture(String name, File inputs)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      Log.reset();
      // The extension that writes the manager's events into the same log the
      // test bodies write into. Registered the way a third party registers one,
      // through a META-INF/services file on the thread context class loader —
      // which is also the loader the integration's own service lookups read, so
      // installing it is part of standing where the shipped code stands.
      Env env = new Env();
      boolean known;
      try
      {
         env.service(Profile.class, Events.class).install();
         known = junit(r) || testng(r) || direct(r);
      }
      finally
      {
         env.restore();
         r.list("log", Log.lines());
         delete(env.root());
      }
      if (!known)
      {
         throw new IllegalArgumentException("unknown runner fixture: " + name);
      }
   }

   /** Recursively removes the overlay directory. */
   private static void delete(File f)
   {
      File[] children = f.listFiles();
      if (children != null)
      {
         for (File child : children)
         {
            delete(child);
         }
      }
      f.delete();
   }

   // -------------------------------------------------------------------- junit

   private boolean junit(Rec r) throws Throwable
   {
      if (!name.startsWith("junit-"))
      {
         return false;
      }
      String kase = name.substring("junit-".length());
      TestRunner runner = new JUnitTestRunner();

      if ("pass".equals(kase))
      {
         execute(r, runner, JUnitCases.Pass.class, "shouldPass", false);
      }
      else if ("select-first".equals(kase))
      {
         execute(r, runner, JUnitCases.TwoMethods.class, "first", false);
      }
      else if ("select-second".equals(kase))
      {
         execute(r, runner, JUnitCases.TwoMethods.class, "second", false);
      }
      else if ("throws".equals(kase))
      {
         execute(r, runner, JUnitCases.Throws.class, "shouldThrow", false);
      }
      else if ("throws-error".equals(kase))
      {
         execute(r, runner, JUnitCases.ThrowsError.class, "shouldThrow", false);
      }
      else if ("expected-thrown".equals(kase))
      {
         execute(r, runner, JUnitCases.ExpectedThrown.class, "shouldThrowExpected", false);
      }
      else if ("expected-not-thrown".equals(kase))
      {
         // JUnit composes the complaint about the missing exception, so only
         // the type chain is recorded.
         execute(r, runner, JUnitCases.ExpectedNotThrown.class, "shouldNotThrow", true);
      }
      else if ("ignored".equals(kase))
      {
         execute(r, runner, JUnitCases.Ignored.class, "shouldBeIgnored", false);
      }
      else if ("assumption-fails".equals(kase))
      {
         execute(r, runner, JUnitCases.AssumptionFails.class, "shouldBeAssumedAway", false);
      }
      else if ("lifecycle".equals(kase))
      {
         execute(r, runner, JUnitCases.Lifecycle.class, "shouldPass", false);
      }
      else if ("before-throws".equals(kase))
      {
         execute(r, runner, JUnitCases.BeforeThrows.class, "shouldPass", false);
      }
      else if ("after-throws".equals(kase))
      {
         execute(r, runner, JUnitCases.AfterThrows.class, "shouldPass", false);
      }
      else if ("both-throw".equals(kase))
      {
         execute(r, runner, JUnitCases.BothThrow.class, "shouldThrow", false);
      }
      else if ("error-skips-after".equals(kase))
      {
         // The pair to both-throw: same shape, but the body throws an Error.
         // What differs between the two transcripts is the answer.
         execute(r, runner, JUnitCases.ErrorWithAfter.class, "shouldThrow", false);
      }
      else if ("takes-argument".equals(kase))
      {
         // Reflection composes the complaint about the argument count.
         execute(r, runner, JUnitCases.TakesArgument.class, "shouldTakeAnArgument", true);
      }
      else if ("plain-class".equals(kase))
      {
         execute(r, runner, JUnitCases.Plain.class, "shouldPass", false);
      }
      else if ("no-test-methods".equals(kase))
      {
         execute(r, runner, JUnitCases.NoTestMethods.class, "notATest", true);
      }
      else if ("unknown-method".equals(kase))
      {
         execute(r, runner, JUnitCases.Pass.class, "noSuchMethod", true);
      }
      else if ("twice".equals(kase))
      {
         // Two executes on one thread. The integration keeps its adaptor and
         // its "last created runner" in ThreadLocals and clears both when the
         // run finishes, so the second execute is either a clean second run or
         // a refusal, and which one it is cannot be read off the source alone.
         execute(r, runner, JUnitCases.Pass.class, "shouldPass", false);
         r.line("-- second execute, same thread, same runner --");
         execute(r, runner, JUnitCases.Pass.class, "shouldPass", false);
      }
      else if ("extra-listener".equals(kase))
      {
         execute(r, new Runners.Listening(), JUnitCases.Lifecycle.class, "shouldPass", false);
      }
      else if ("extra-listener-expected".equals(kase))
      {
         // The interesting one: the holder consumes caughtTestException in its
         // own testFinished, and the overridden listener is registered after it.
         execute(r, new Runners.Listening(), JUnitCases.ExpectedThrown.class,
               "shouldThrowExpected", false);
      }
      else if ("extra-listener-ignored".equals(kase))
      {
         execute(r, new Runners.Listening(), JUnitCases.Ignored.class, "shouldBeIgnored", false);
      }
      else if ("client-profile".equals(kase))
      {
         // The manager this builds is the client one, which registers no
         // container because no arquillian.xml names one. Everything the
         // transcript shows is that switch's consequence.
         execute(r, new Runners.OnClientProfile(), JUnitCases.Pass.class, "shouldPass", false);
      }
      else
      {
         return false;
      }
      return true;
   }

   // ------------------------------------------------------------------- testng

   private boolean testng(Rec r) throws Throwable
   {
      if (!name.startsWith("testng-"))
      {
         return false;
      }
      String kase = name.substring("testng-".length());
      TestRunner runner = new TestNGTestRunner();

      if ("pass".equals(kase))
      {
         execute(r, runner, TestNGCases.Pass.class, "shouldPass", false);
      }
      else if ("select-first".equals(kase))
      {
         execute(r, runner, TestNGCases.TwoMethods.class, "first", false);
      }
      else if ("select-second".equals(kase))
      {
         execute(r, runner, TestNGCases.TwoMethods.class, "second", false);
      }
      else if ("throws".equals(kase))
      {
         execute(r, runner, TestNGCases.Throws.class, "shouldThrow", false);
      }
      else if ("throws-error".equals(kase))
      {
         execute(r, runner, TestNGCases.ThrowsError.class, "shouldThrow", false);
      }
      else if ("expected-thrown".equals(kase))
      {
         execute(r, runner, TestNGCases.ExpectedThrown.class, "shouldThrowExpected", false);
      }
      else if ("skipped".equals(kase))
      {
         // The SkipException is this suite's to read: it was thrown by the
         // fixture's own body, even though the type is TestNG's.
         execute(r, runner, TestNGCases.Skipped.class, "shouldSkip", false);
      }
      else if ("lifecycle".equals(kase))
      {
         execute(r, runner, TestNGCases.Lifecycle.class, "shouldPass", false);
      }
      else if ("before-throws".equals(kase))
      {
         execute(r, runner, TestNGCases.BeforeThrows.class, "shouldPass", false);
      }
      else if ("after-throws".equals(kase))
      {
         execute(r, runner, TestNGCases.AfterThrows.class, "shouldPass", false);
      }
      else if ("depends-method-removed".equals(kase))
      {
         execute(r, runner, TestNGCases.DependsOnMethod.class, "shouldRunAnyway", false);
      }
      else if ("depends-group-removed".equals(kase))
      {
         execute(r, runner, TestNGCases.DependsOnGroup.class, "shouldRunAnyway", false);
      }
      else if ("takes-argument".equals(kase))
      {
         // TestNG composes the complaint about the parameter it cannot supply.
         execute(r, runner, TestNGCases.TakesArgument.class, "shouldTakeAnArgument", true);
      }
      else if ("data-provider".equals(kase))
      {
         execute(r, runner, TestNGCases.DataProvider.class, "shouldTakeAnArgument", false);
      }
      else if ("no-adaptor".equals(kase))
      {
         execute(r, runner, TestNGCases.NoAdaptor.class, "shouldPass", false);
      }
      else if ("no-test-methods".equals(kase))
      {
         // Nothing ran, so the listener's context holds no results at all and
         // the last branch of getTestResult composes a sentence around TestNG's
         // own toString of that context. Types only.
         execute(r, runner, TestNGCases.NoTestMethods.class, "notATest", true);
      }
      else if ("unknown-method".equals(kase))
      {
         execute(r, runner, TestNGCases.Pass.class, "noSuchMethod", true);
      }
      else if ("twice".equals(kase))
      {
         // The TestNG side keeps its adaptor in a ThreadLocal on the abstract
         // base class and clears it in @AfterSuite, so the same question as the
         // junit side's twice: clean second run, or a run against a shut-down
         // manager.
         execute(r, runner, TestNGCases.Pass.class, "shouldPass", false);
         r.line("-- second execute, same thread, same runner --");
         execute(r, runner, TestNGCases.Pass.class, "shouldPass", false);
      }
      else
      {
         return false;
      }
      return true;
   }

   // ------------------------------------------------------------------- direct

   /**
    * The pieces of the TestNG integration the shipped runner does not reach.
    *
    * {@code RemoveDependsOnTransformer} is registered and its effect is already
    * recorded by the {@code testng-depends-*} fixtures, but not which calls
    * produced it, and not what the absence of it would look like — the
    * {@code depends-with/without-transformer} pair supplies the second;
    * {@code TestDataProviderTransformer} and {@code TestEnricherDataProvider}
    * are registered by nothing at all in this tree. They are still shipped API —
    * public classes in a published module — so a migration can break them, and
    * a suite that never called them would not notice.
    */
   private boolean direct(Rec r) throws Throwable
   {
      if (!name.startsWith("direct-"))
      {
         return false;
      }
      String kase = name.substring("direct-".length());

      if ("remove-depends".equals(kase))
      {
         ITestAnnotation annotation = Transformers.recording();
         r.kv("transformer", RemoveDependsOnTransformer.class.getName());
         new RemoveDependsOnTransformer().transform(annotation, JUnitCases.Pass.class, null, null);
         // Every call it made is in the log, including the emptiness of the
         // arrays it passed, which is the whole of what it does.
      }
      else if ("data-provider-transformer".equals(kase))
      {
         ITestAnnotation annotation = Transformers.recording();
         r.kv("transformer", TestDataProviderTransformer.class.getName());
         new TestDataProviderTransformer().transform(annotation, JUnitCases.Pass.class, null, null);
         // A proxy is not a TestAnnotation, so the guard holds and the only
         // call in the log is the getter the guard is reached through.
      }
      else if ("append-junit".equals(kase))
      {
         Appenders.record(r, new JUnitDeploymentAppender(),
               "/org/jboss/arquillian/junit/",
               new String[] {
                     "/junit/framework/Assert.class",
                     "/junit/extensions/TestSetup.class",
                     "/junit/runner/BaseTestRunner.class",
                     "/org/junit/Assert.class",
                     "/org/junit/runner/RunWith.class",
                     "/org/hamcrest/Matcher.class" },
               // This appender excludes nothing, which is itself the difference
               // between the two: it packages whatever the packages hold.
               new String[0]);
      }
      else if ("append-testng".equals(kase))
      {
         Appenders.record(r, new TestNGDeploymentAppender(),
               "/org/jboss/arquillian/testng/",
               new String[] {
                     "/org/testng/TestNG.class",
                     "/org/testng/annotations/Test.class",
                     "/org/testng/internal/AnnotationTypeEnum.class",
                     "/org/testng/xml/XmlSuite.class",
                     // Excluded by the first filter: present in the TestNG jar,
                     // absent here. The pair of markers is what makes the
                     // filtered count below mean something.
                     "/org/testng/junit/JUnitTestRunner.class",
                     "/com/google/inject/Inject.class",
                     // The outer class of the one the second filter names. It
                     // stays, which is how the transcript shows that filter is
                     // surgical rather than a blanket "no Guice".
                     "/com/google/inject/InterceptorStackCallback.class",
                     "/com/beust/jcommander/JCommander.class" },
               // The three exclusions the appender's source explains, each
               // expected to have removed everything it names.
               new String[] {
                     "/org/testng/junit/",
                     "/org/testng/eclipse/",
                     "InterceptorStackCallback$InterceptedMethodInvocation" });
      }
      else if ("data-provider-shape".equals(kase))
      {
         for (String m : new String[] { "none", "one", "two" })
         {
            Method method = Transformers.Arities.class.getMethod(m,
                  "none".equals(m) ? new Class<?>[0]
                        : "one".equals(m) ? new Class<?>[] { String.class }
                              : new Class<?>[] { String.class, int.class });
            Object[][] values = TestEnricherDataProvider.enrich(method);
            r.kv(m + ".rows", Integer.valueOf(values.length));
            r.kv(m + ".cols", Integer.valueOf(values[0].length));
            r.list(m + ".row0", Arrays.asList(values[0]));
         }
         r.kv("providerName", TestEnricherDataProvider.PROVIDER_NAME);
      }
      else if ("depends-with-transformer".equals(kase))
      {
         dependsControl(r, true);
      }
      else if ("depends-without-transformer".equals(kase))
      {
         dependsControl(r, false);
      }
      else
      {
         return false;
      }
      return true;
   }

   /**
    * The same run twice, differing only in whether the transformer is registered.
    *
    * <p><b>Why this pair exists.</b> {@code testng-depends-method-removed} records
    * a PASSED, and a PASSED on its own is the least informative thing a fixture
    * can say: it is equally consistent with "the transformer stripped the
    * dependency" and with "TestNG never minded". Only the arm without the
    * transformer distinguishes them, so both arms are recorded and the finding is
    * the diff between them rather than an assertion in a comment. A transformer
    * that silently stopped being applied — a real outcome of a TestNG upgrade
    * changing when {@code IAnnotationTransformer}s run — would flip exactly one of
    * these two transcripts and leave the other alone.
    *
    * <p><b>Why it does not reuse {@code TestNGTestRunner}.</b> It cannot: the
    * runner registers the transformer unconditionally, so the negative arm has to
    * build the suite itself. The construction below is the runner's own
    * {@code createSuite} with one line left out — {@code setAnnotations(
    * AnnotationTypeEnum.JDK.getName())}, which names a TestNG <em>internal</em>
    * that later versions relocate. Leaving it out of both arms keeps the harness
    * compiling across the framework bump and costs nothing, because whatever that
    * omission changes it changes identically on both sides; the contrast is
    * between the arms, not between an arm and the shipped runner.
    */
   private void dependsControl(Rec r, boolean withTransformer)
   {
      r.kv("case", TestNGCases.DependsOnMethod.class.getName());
      r.kv("method", "shouldRunAnyway");
      r.kv("dependsOnMethods", "noSuchMethod");
      r.kv("transformer", Boolean.valueOf(withTransformer));

      // The runner sets this on the way in and clears it on the way out; a suite
      // built by hand has to do the same or the base class boots the wrong half
      // of the manager.
      DeployableTestBuilder.setProfile(ContainerProfile.CONTAINER);
      try
      {
         TestListener listener = new TestListener();
         TestNG runner = new TestNG(false);
         runner.setVerbose(0);
         runner.addListener(listener);
         if (withTransformer)
         {
            runner.addListener(new RemoveDependsOnTransformer());
         }

         XmlSuite suite = new XmlSuite();
         suite.setName("Arquillian");
         XmlTest test = new XmlTest(suite);
         test.setName("Arquillian - depends control");
         XmlClass xmlClass = new XmlClass(TestNGCases.DependsOnMethod.class);
         xmlClass.getIncludedMethods().add(new XmlInclude("shouldRunAnyway"));
         test.setXmlClasses(Arrays.asList(xmlClass));
         runner.setXmlSuites(Arrays.asList(suite));

         try
         {
            runner.run();
            r.kv("run", "returned");
         }
         catch (Throwable t)
         {
            // TestNG's own complaint about a dependency it cannot resolve.
            r.kv("run", "threw");
            r.thrownTypes("run.ex", t);
            return;
         }

         TestResult result;
         try
         {
            result = listener.getTestResult();
         }
         catch (Throwable t)
         {
            r.kv("result", "threw");
            r.thrownTypes("result.ex", t);
            return;
         }
         r.kv("result.status", result.getStatus());
         r.kv("result.hasThrowable", Boolean.valueOf(result.getThrowable() != null));
         r.thrownTypes("result.throwable", result.getThrowable());
      }
      finally
      {
         DeployableTestBuilder.clearProfile();
      }
   }

   // ------------------------------------------------------------------ recording

   /**
    * One {@code execute}, recorded.
    *
    * The profile is read before and after. {@code execute} sets it on the way in
    * and clears it on the way out, and both halves are observable through
    * {@code DeployableTestBuilder} — which is the class the integration itself
    * calls, not a back door. A migration that stopped clearing it would leave
    * the next run in this JVM on the wrong profile, and nothing in the returned
    * result would say so.
    */
   private void execute(Rec r, TestRunner runner, Class<?> testClass, String method,
         boolean frameworkComposed)
   {
      r.kv("runner", runner.getClass().getName());
      r.kv("testClass", testClass.getName());
      r.kv("method", method);
      r.kv("profile.before", DeployableTestBuilder.getProfile());
      TestResult result;
      try
      {
         result = runner.execute(testClass, method);
         r.kv("execute", "returned");
      }
      catch (Throwable t)
      {
         r.kv("execute", "threw");
         r.thrownTypes("execute.ex", t);
         r.kv("profile.after", DeployableTestBuilder.getProfile());
         return;
      }
      r.kv("profile.after", DeployableTestBuilder.getProfile());
      if (result == null)
      {
         r.kv("result", "<null>");
         return;
      }
      // Status and throwable only: getStart and getEnd come from
      // System.currentTimeMillis() in the constructor.
      r.kv("result.status", result.getStatus());
      r.kv("result.hasThrowable", Boolean.valueOf(result.getThrowable() != null));
      if (frameworkComposed)
      {
         r.thrownTypes("result.throwable", result.getThrowable());
      }
      else
      {
         r.thrown("result.throwable", result.getThrowable());
      }
   }
}
