package arqprobe.fam;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.life.Conf;
import arqprobe.fam.life.Containers;
import arqprobe.fam.life.Env;
import arqprobe.fam.life.Log;
import arqprobe.fam.life.Run;
import arqprobe.fam.life.Tests;
import arqprobe.fam.life.Trace;

import org.jboss.arquillian.spi.Profile;

/**
 * A whole client run, from {@code arquillian.xml} to a test result.
 *
 * Everything else in this suite takes one piece of the tree and asks it a
 * question directly. This family asks the only question a user of Arquillian
 * actually asks: given this configuration and this test class, what happens —
 * which container callbacks fire, in what order, what gets deployed, where the
 * test method runs, and what comes back. It is driven through
 * {@code TestRunnerAdaptor}, the same four-method surface the JUnit and TestNG
 * integrations are built on, so nothing here reaches past a supported seam.
 *
 * Three pieces make that possible and each of them is a real extension point
 * rather than a test hook: a {@code DeployableContainer} whose behaviour is
 * declared in the configuration file it is handed, a {@code Profile} service
 * that observes the manager's events, and a class loader overlay carrying the
 * configuration. A user shipping a container adapter writes all three.
 *
 * The recorded transcript is one ordered log. Container callbacks, manager
 * events, lifecycle callbacks and test bodies all append to it, so the recording
 * captures interleaving and not just outcomes.
 *
 * Code-driven family — the fixture name selects the scenario.
 */
public class LifeFixture implements Fixture
{
   private final String name;

   public LifeFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Exception
   {
      Log.reset();
      Env env = new Env();
      boolean known;
      try
      {
         env.service(Profile.class, Trace.class);
         known = boot(r, env) || lifecycle(r, env) || execution(r, env)
               || deployment(r, env) || enrichment(r, env) || extension(r, env);
      }
      finally
      {
         env.restore();
         r.list("log", Log.lines());
         delete(env.root());
      }
      if (!known)
      {
         throw new IllegalArgumentException("unknown life fixture: " + name);
      }
   }

   // ------------------------------------------------------- booting a manager

   /**
    * Fixtures that only boot.
    *
    * {@code beforeSuite} is where the registry's contents become visible: it is
    * what makes the controller set up and start every container the
    * configuration selected, so a boot fixture that fires it and nothing else
    * records exactly which containers the selection rules chose.
    */
   private boolean boot(Rec r, Env env) throws Exception
   {
      if ("boot-default-container".equals(name))
      {
         env.arquillianXml(one().text());
         suite(env);
         return true;
      }
      if ("boot-no-configuration".equals(name))
      {
         // No arquillian.xml at all. Nothing is selected, so the creator falls
         // back to the only registered adapter and invents a container for it,
         // which leaves the adapter holding a configuration nobody wrote.
         suite(env);
         return true;
      }
      if ("boot-launch-selects".equals(name))
      {
         env.arquillianXml(two().text());
         env.launch("beta");
         suite(env);
         return true;
      }
      if ("boot-launch-unknown".equals(name))
      {
         env.arquillianXml(two().text());
         env.launch("gamma");
         suite(env);
         return true;
      }
      if ("boot-launch-commented".equals(name))
      {
         // The launch file skips comment lines and takes the first that is not
         // one, so this selects alpha — not the comment above it, and not the
         // beta below it either.
         env.arquillianXml(two().text());
         env.launch("# beta\nalpha\nbeta\n");
         suite(env);
         return true;
      }
      if ("boot-launch-group".equals(name))
      {
         env.arquillianXml(group().text());
         env.launch("both");
         suite(env);
         return true;
      }
      if ("boot-two-containers-one-default".equals(name))
      {
         Conf c = Conf.arquillian();
         c.container("alpha").named().end();
         c.container("beta").asDefault().named().end();
         env.arquillianXml(c.text());
         suite(env);
         return true;
      }
      if ("boot-two-containers-no-default".equals(name))
      {
         env.arquillianXml(two().text());
         suite(env);
         return true;
      }
      if ("boot-two-containers-both-default".equals(name))
      {
         Conf c = Conf.arquillian();
         c.container("alpha").asDefault().named().end();
         c.container("beta").asDefault().named().end();
         env.arquillianXml(c.text());
         suite(env);
         return true;
      }
      if ("boot-configuration-refused".equals(name))
      {
         env.arquillianXml(one("modeValidate", Containers.FAIL).text());
         suite(env);
         return true;
      }
      if ("boot-unknown-property".equals(name))
      {
         // A property the configuration class has no setter for, alongside one
         // it does. The known one is what makes the answer readable: without it
         // a dropped property and a refused document look the same.
         Conf c = Conf.arquillian();
         c.container("alpha").asDefault().named()
          .property("thereIsNoSuchProperty", "value").property("weight", "7").end();
         env.arquillianXml(c.text());
         suite(env);
         return true;
      }
      if ("boot-typed-properties".equals(name))
      {
         Conf c = Conf.arquillian();
         c.container("alpha").asDefault().named()
          .property("weight", "42").property("chatty", "true").end();
         env.arquillianXml(c.text());
         suite(env);
         return true;
      }
      if ("boot-typed-property-unparsable".equals(name))
      {
         env.arquillianXml(one("weight", "heavy").text());
         suite(env);
         return true;
      }
      if ("boot-default-protocol".equals(name))
      {
         env.arquillianXml(one().defaultProtocol("Local").text());
         suite(env);
         return true;
      }
      if ("boot-default-protocol-unknown".equals(name))
      {
         env.arquillianXml(one().defaultProtocol("Telepathy").text());
         suite(env);
         return true;
      }
      return false;
   }

   // ------------------------------------------------------- the run lifecycle

   private boolean lifecycle(Rec r, Env env) throws Exception
   {
      if ("life-full-sequence".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.Plain.class, "passes");
         return true;
      }
      if ("life-setup-refused".equals(name))
      {
         env.arquillianXml(one("modeSetup", Containers.FAIL).text());
         full(env, Tests.Plain.class, "passes");
         return true;
      }
      if ("life-start-refused".equals(name))
      {
         env.arquillianXml(one("modeStart", Containers.FAIL).text());
         full(env, Tests.Plain.class, "passes");
         return true;
      }
      if ("life-stop-refused".equals(name))
      {
         env.arquillianXml(one("modeStop", Containers.FAIL).text());
         full(env, Tests.Plain.class, "passes");
         return true;
      }
      if ("life-deploy-refused".equals(name))
      {
         env.arquillianXml(one("modeDeploy", Containers.FAIL).text());
         full(env, Tests.Plain.class, "passes");
         return true;
      }
      if ("life-undeploy-refused".equals(name))
      {
         env.arquillianXml(one("modeUnDeploy", Containers.FAIL).text());
         full(env, Tests.Plain.class, "passes");
         return true;
      }
      if ("life-two-classes".equals(name))
      {
         env.arquillianXml(one().text());
         env.install();
         Run run = new Run();
         run.build();
         run.beforeSuite();
         clazz(run, Tests.Plain.class, "passes");
         clazz(run, Tests.Failing.class, "fails");
         run.afterSuite();
         run.shutdown();
         return true;
      }
      if ("life-shutdown-only".equals(name))
      {
         env.arquillianXml(one().text());
         env.install();
         Run run = new Run();
         run.build();
         run.shutdown();
         return true;
      }
      if ("life-class-without-suite".equals(name))
      {
         env.arquillianXml(one().text());
         env.install();
         Run run = new Run();
         run.build();
         clazz(run, Tests.Plain.class, "passes");
         run.shutdown();
         return true;
      }
      if ("life-test-without-before".equals(name))
      {
         env.arquillianXml(one().text());
         env.install();
         Run run = new Run();
         run.build();
         run.beforeSuite();
         run.beforeClass(Tests.Plain.class);
         run.test(instance(Tests.Plain.class), "passes");
         run.afterClass(Tests.Plain.class);
         run.afterSuite();
         run.shutdown();
         return true;
      }
      if ("life-after-without-before".equals(name))
      {
         env.arquillianXml(one().text());
         env.install();
         Run run = new Run();
         run.build();
         run.beforeSuite();
         run.beforeClass(Tests.Plain.class);
         run.after(instance(Tests.Plain.class), "passes");
         run.afterClass(Tests.Plain.class);
         run.afterSuite();
         run.shutdown();
         return true;
      }
      if ("life-suite-twice".equals(name))
      {
         env.arquillianXml(one().text());
         env.install();
         Run run = new Run();
         run.build();
         run.beforeSuite();
         run.beforeSuite();
         run.afterSuite();
         run.afterSuite();
         run.shutdown();
         return true;
      }
      if ("life-lifecycle-executors-ignored".equals(name))
      {
         // Every lifecycle executor the adaptor is handed logs its own name and
         // then throws. None of the four lines appears and the run finishes
         // clean, because the client profile has no observer that invokes them:
         // Before/AfterLifecycleEventExecuter is a container-profile service, so
         // on the client the executors travel with the events and are dropped.
         //
         // One fixture rather than one per position. Refusing at each point
         // separately produced four transcripts identical to the plain run,
         // which recorded the executors being ignored four times over and the
         // difference between the positions not at all.
         env.arquillianXml(one().text());
         env.install();
         Run run = new Run();
         run.build();
         run.beforeSuite();
         run.beforeClass(Tests.Plain.class, Tests.refusing("beforeClass:Plain"));
         Object o = instance(Tests.Plain.class);
         run.before(o, "passes", Tests.refusing("before:passes"));
         run.test(o, "passes");
         run.after(o, "passes", Tests.refusing("after:passes"));
         run.afterClass(Tests.Plain.class, Tests.refusing("afterClass:Plain"));
         run.afterSuite();
         run.shutdown();
         // The claim as a number rather than as an absence. Four executors were
         // handed over and each logs before it throws, so "none of them ran" is
         // a count that has to change if the client profile ever starts
         // invoking them — where a transcript identical to the plain run says
         // nothing at all, and is what this fixture recorded before.
         List<String> duringRun = invoked();
         // And the count is only worth reading if a non-zero one was reachable.
         // An executor of the same kind, invoked by hand, writes its line: a
         // zero above therefore means "the run did not call them" and not "the
         // executors were never wired to the log in the first place".
         try
         {
            Tests.lifecycle("control").invoke();
         }
         catch (Throwable t)
         {
            throw new IllegalStateException("the control executor threw", t);
         }
         r.sorted("lifecycle.invoked", duringRun);
         r.sorted("lifecycle.control", invoked());
         return true;
      }
      return false;
   }

   // --------------------------------------------------- where the test method runs

   private boolean execution(Rec r, Env env) throws Exception
   {
      // The baseline — testable deployment, no client annotation, so the call
      // is routed through the protocol rather than made directly — is
      // life-full-sequence. A fixture of its own here recorded the same bytes
      // under a second name.
      if ("exec-client-class".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.ClientClass.class, "passes");
         return true;
      }
      if ("exec-client-method".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.ClientMethod.class, "here", "there");
         return true;
      }
      if ("exec-not-testable".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.NotTestable.class, "passes");
         return true;
      }
      if ("exec-failing-test".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.Failing.class, "fails");
         return true;
      }
      if ("exec-client-failing".equals(name))
      {
         // The same throw, raised on the other side of the protocol decision:
         // what a failure looks like when nothing serialised it.
         env.arquillianXml(one().text());
         full(env, Tests.ClientClass.class, "fails");
         return true;
      }
      if ("exec-protocol-unknown".equals(name))
      {
         // The adapter claims a protocol nothing implements. Nothing notices
         // until a testable deployment has to be executed over it.
         env.arquillianXml(one("protocol", "Telepathy").text());
         full(env, Tests.Plain.class, "passes");
         return true;
      }
      return false;
   }

   // ----------------------------------------------------------- what gets deployed

   private boolean deployment(Rec r, Env env) throws Exception
   {
      if ("dep-two-deployments".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.TwoDeployments.class, "onTwo", "onDefault");
         return true;
      }
      if ("dep-targets-container".equals(name))
      {
         env.arquillianXml(group().text());
         env.launch("both");
         full(env, Tests.Targeted.class, "passes");
         return true;
      }
      if ("dep-targets-unknown".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.TargetsUnknown.class, "passes");
         return true;
      }
      if ("dep-over-protocol".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.OverLocal.class, "passes");
         return true;
      }
      if ("dep-over-unknown".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.OverUnknown.class, "passes");
         return true;
      }
      if ("dep-descriptor".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.DescriptorDeployment.class, "passes");
         return true;
      }
      if ("dep-throwing-method".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.ThrowingDeployment.class, "passes");
         return true;
      }
      if ("dep-none".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.NoDeployment.class, "passes");
         return true;
      }
      if ("dep-should-throw-not-thrown".equals(name))
      {
         // The deployment is declared to fail and does not.
         env.arquillianXml(one().text());
         full(env, Tests.ShouldThrow.class, "passes");
         return true;
      }
      if ("dep-should-throw-thrown".equals(name))
      {
         env.arquillianXml(one("modeDeploy", Containers.FAIL).text());
         full(env, Tests.ShouldThrow.class, "passes");
         return true;
      }
      if ("dep-export".equals(name))
      {
         File out = new File(env.root(), "exported");
         env.arquillianXml(one().deploymentExportPath(out.getAbsolutePath()).text());
         full(env, Tests.Plain.class, "passes");
         // The names only. The archives themselves are the whole of Arquillian
         // plus the whole of JUnit and TestNG, and their contents would change
         // with any of those.
         r.sorted("exported", names(out));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------- what gets injected

   private boolean enrichment(Rec r, Env env) throws Exception
   {
      if ("enrich-url-field".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.WantsUrl.class, "passes");
         return true;
      }
      if ("enrich-uri-field".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.WantsUri.class, "passes");
         return true;
      }
      if ("enrich-url-argument".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.WantsUrlArgument.class, "passes");
         return true;
      }
      if ("enrich-deployer-field".equals(name))
      {
         // A provider claims the type and then has nothing to hand back,
         // because nothing in the client profile produces a Deployer.
         env.arquillianXml(one().text());
         full(env, Tests.WantsDeployer.class, "passes");
         return true;
      }
      if ("enrich-unclaimed-field".equals(name))
      {
         env.arquillianXml(one().text());
         full(env, Tests.WantsUnclaimed.class, "passes");
         return true;
      }
      if ("enrich-no-deployment".equals(name))
      {
         // Asking for the deployment's URL when there is no deployment.
         env.arquillianXml(one("modeDeploy", Containers.FAIL).text());
         full(env, Tests.WantsUrl.class, "passes");
         return true;
      }
      return false;
   }

   // ----------------------------------------------------------- other extensions

   private boolean extension(Rec r, Env env) throws Exception
   {
      if ("ext-two-profiles".equals(name))
      {
         env.service(Profile.class, Trace.class, Trace.Second.class);
         env.arquillianXml(one().text());
         full(env, Tests.Plain.class, "passes");
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------ plumbing

   /** One container, selected by its default flag, naming itself. */
   private static Conf one()
   {
      Conf c = Conf.arquillian();
      c.container("alpha").asDefault().named().end();
      return c;
   }

   private static Conf one(String property, String value)
   {
      Conf c = Conf.arquillian();
      c.container("alpha").asDefault().named().property(property, value).end();
      return c;
   }

   /** Two containers, neither marked default. */
   private static Conf two()
   {
      Conf c = Conf.arquillian();
      c.container("alpha").named().end();
      c.container("beta").named().end();
      return c;
   }

   /** Two containers in one group, selected together by name. */
   private static Conf group()
   {
      Conf c = Conf.arquillian();
      Conf.Group g = c.group("both");
      g.container("alpha").named().end();
      g.container("beta").named().end();
      g.end();
      return c;
   }

   /** Boot, open and close the suite, shut down. */
   private static void suite(Env env) throws Exception
   {
      env.install();
      Run run = new Run();
      run.build();
      run.beforeSuite();
      run.afterSuite();
      run.shutdown();
   }

   /** The whole of a run over one test class. */
   private static void full(Env env, Class<?> testClass, String... methods) throws Exception
   {
      env.install();
      Run run = new Run();
      run.build();
      run.beforeSuite();
      clazz(run, testClass, methods);
      run.afterSuite();
      run.shutdown();
   }

   /** One class within an already-open suite. */
   private static void clazz(Run run, Class<?> testClass, String... methods) throws Exception
   {
      run.beforeClass(testClass);
      Object o = instance(testClass);
      for (String method : methods)
      {
         run.method(o, method);
      }
      run.afterClass(testClass);
   }

   /**
    * One instance per class rather than one per method.
    *
    * JUnit would make a fresh instance for each; nothing in this family depends
    * on which, and reusing one keeps field injection visible across the methods
    * that share it.
    */
   private static Object instance(Class<?> testClass) throws Exception
   {
      return testClass.getDeclaredConstructor().newInstance();
   }

   /** The lifecycle executors that logged themselves, if any did. */
   private static List<String> invoked()
   {
      List<String> out = new ArrayList<String>();
      for (String line : Log.lines())
      {
         if (line.startsWith("lifecycle:"))
         {
            out.add(line);
         }
      }
      return out;
   }

   private static List<String> names(File dir)
   {
      List<String> out = new ArrayList<String>();
      File[] files = dir.listFiles();
      if (files == null)
      {
         return out;
      }
      for (File f : files)
      {
         out.add(f.getName());
      }
      return out;
   }

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
}
