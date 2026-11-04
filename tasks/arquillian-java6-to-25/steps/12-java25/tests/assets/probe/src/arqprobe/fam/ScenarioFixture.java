package arqprobe.fam;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.scenario.TestClasses;

import org.jboss.arquillian.impl.client.deployment.AnnotationDeploymentScenarioGenerator;
import org.jboss.arquillian.spi.TestClass;
import org.jboss.arquillian.spi.client.deployment.DeploymentDescription;
import org.jboss.arquillian.spi.client.deployment.DeploymentScenario;
import org.jboss.arquillian.spi.client.protocol.ProtocolDescription;
import org.jboss.arquillian.spi.client.test.DeploymentTargetDescription;
import org.jboss.arquillian.spi.client.test.TargetDescription;
import org.jboss.shrinkwrap.api.Archive;
import org.jboss.shrinkwrap.api.ArchivePath;

/**
 * The annotation-driven deployment scenario generator, and the scenario model it
 * produces: which {@code @Deployment} shapes are accepted, what each one is
 * described as, and how the scenario answers lookups.
 *
 * Generated deployments are recorded sorted by name. The generator walks
 * {@code Class.getMethods()}, whose order the JDK does not specify, so list
 * position is not part of the contract — membership and content are.
 */
public class ScenarioFixture implements Fixture
{
   private final String name;

   public ScenarioFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      if (generateFixture(r) || lookupFixture(r) || modelFixture(r))
      {
         return;
      }
      throw new IllegalArgumentException("unknown scenario fixture: " + name);
   }

   // ------------------------------------------------------------------------
   // generation
   // ------------------------------------------------------------------------

   private boolean generateFixture(Rec r)
   {
      Class<?> target = generatorTarget();
      if (target == null)
      {
         return false;
      }
      DeploymentScenario scenario = generate(r, target);
      if (scenario != null)
      {
         dumpScenario(r, scenario);
      }
      return true;
   }

   private Class<?> generatorTarget()
   {
      if ("single-archive".equals(name))
      {
         return TestClasses.SingleArchive.class;
      }
      if ("single-web-archive".equals(name))
      {
         return TestClasses.SingleWebArchive.class;
      }
      if ("single-descriptor".equals(name))
      {
         return TestClasses.SingleDescriptor.class;
      }
      if ("archive-interface-return".equals(name))
      {
         return TestClasses.ArchiveInterfaceReturn.class;
      }
      if ("named-deployment".equals(name))
      {
         return TestClasses.NamedDeployment.class;
      }
      if ("not-testable".equals(name))
      {
         return TestClasses.NotTestable.class;
      }
      if ("not-managed".equals(name))
      {
         return TestClasses.NotManaged.class;
      }
      if ("explicit-order".equals(name))
      {
         return TestClasses.ExplicitOrder.class;
      }
      if ("targets-container".equals(name))
      {
         return TestClasses.TargetedDeployment.class;
      }
      if ("over-protocol".equals(name))
      {
         return TestClasses.ProtocolDeployment.class;
      }
      if ("should-throw-exception".equals(name))
      {
         return TestClasses.ShouldThrow.class;
      }
      if ("should-throw-overrides-testable".equals(name))
      {
         return TestClasses.ShouldThrowButTestable.class;
      }
      if ("all-attributes".equals(name))
      {
         return TestClasses.AllAttributes.class;
      }
      if ("two-deployments".equals(name))
      {
         return TestClasses.TwoDeployments.class;
      }
      if ("two-ordered-deployments".equals(name))
      {
         return TestClasses.TwoOrderedDeployments.class;
      }
      if ("mixed-managed-deployments".equals(name))
      {
         return TestClasses.MixedManagedDeployments.class;
      }
      if ("two-targets".equals(name))
      {
         return TestClasses.TwoTargets.class;
      }
      if ("two-protocols".equals(name))
      {
         return TestClasses.TwoProtocols.class;
      }
      if ("inherited-deployment".equals(name))
      {
         return TestClasses.InheritedDeployment.class;
      }
      if ("duplicate-names".equals(name))
      {
         return TestClasses.DuplicateNames.class;
      }
      if ("no-deployment-method".equals(name))
      {
         return TestClasses.NoDeploymentMethod.class;
      }
      if ("non-static-deployment".equals(name))
      {
         return TestClasses.NonStaticDeployment.class;
      }
      if ("wrong-return-type".equals(name))
      {
         return TestClasses.WrongReturnType.class;
      }
      if ("void-return-type".equals(name))
      {
         return TestClasses.VoidReturnType.class;
      }
      if ("deployment-with-parameters".equals(name))
      {
         return TestClasses.WithParameters.class;
      }
      if ("package-private-deployment".equals(name))
      {
         return TestClasses.PrivateStaticDeployment.class;
      }
      if ("throwing-deployment".equals(name))
      {
         return TestClasses.ThrowingDeployment.class;
      }
      if ("null-returning-deployment".equals(name))
      {
         return TestClasses.NullReturningDeployment.class;
      }
      if ("one-valid-one-invalid".equals(name))
      {
         return TestClasses.OneValidOneInvalid.class;
      }
      if ("generate-null-test-class".equals(name))
      {
         return Void.class;
      }
      return null;
   }

   private DeploymentScenario generate(Rec r, final Class<?> target)
   {
      final AnnotationDeploymentScenarioGenerator generator = new AnnotationDeploymentScenarioGenerator();
      if (target == Void.class)
      {
         r.attempt("generate(null)", () -> generator.generate(null));
         r.attempt("generate(TestClass(null))", () -> generator.generate(new TestClass(null)));
         return null;
      }
      return (DeploymentScenario) r.call("generate", () -> generator.generate(new TestClass(target)));
   }

   // ------------------------------------------------------------------------
   // scenario lookups
   // ------------------------------------------------------------------------

   private boolean lookupFixture(Rec r)
   {
      if ("lookup-default-single".equals(name))
      {
         DeploymentScenario s = scenarioOf(TestClasses.SingleArchive.class);
         r.kv("default", nameOf(s.getDeployment(DeploymentTargetDescription.DEFAULT)));
         return true;
      }
      if ("lookup-default-multiple".equals(name))
      {
         DeploymentScenario s = scenarioOf(TestClasses.TwoDeployments.class);
         r.kv("default", nameOf(s.getDeployment(DeploymentTargetDescription.DEFAULT)));
         return true;
      }
      if ("lookup-by-name".equals(name))
      {
         DeploymentScenario s = scenarioOf(TestClasses.TwoDeployments.class);
         r.kv("one", nameOf(s.getDeployment(new DeploymentTargetDescription("one"))));
         r.kv("two", nameOf(s.getDeployment(new DeploymentTargetDescription("two"))));
         return true;
      }
      if ("lookup-missing-name".equals(name))
      {
         DeploymentScenario s = scenarioOf(TestClasses.TwoDeployments.class);
         r.kv("missing", nameOf(s.getDeployment(new DeploymentTargetDescription("three"))));
         r.kv("empty-name", nameOf(s.getDeployment(new DeploymentTargetDescription(""))));
         return true;
      }
      if ("lookup-duplicate-name".equals(name))
      {
         // Two methods declare the same deployment name, so a lookup by that
         // name has two right answers and returns whichever the generator saw
         // first — which follows reflection's method order, and the JVM does
         // not specify that. What the scenario ends up holding is the contract;
         // which duplicate wins the lookup is not, and is not recorded.
         DeploymentScenario s = scenarioOf(TestClasses.DuplicateNames.class);
         List<DeploymentDescription> all = s.getDeployments();
         r.kv("deployments.size", Integer.valueOf(all.size()));
         r.sorted("names", namesOf(all));
         r.sorted("archives", archiveNamesOf(all));
         r.kv("same.found", Boolean.valueOf(
               s.getDeployment(new DeploymentTargetDescription("same")) != null));
         return true;
      }
      if ("lookup-null-target".equals(name))
      {
         final DeploymentScenario s = scenarioOf(TestClasses.SingleArchive.class);
         r.attempt("getDeployment(null)", () -> s.getDeployment(null));
         r.attempt("getStartupDeploymentsFor(null)", () -> s.getStartupDeploymentsFor(null));
         return true;
      }
      if ("startup-deployments-order".equals(name))
      {
         DeploymentScenario s = scenarioOf(TestClasses.MixedManagedDeployments.class);
         r.list("startup.default", namesOf(s.getStartupDeploymentsFor(TargetDescription.DEFAULT)));
         r.list("startup.other", namesOf(s.getStartupDeploymentsFor(new TargetDescription("nope"))));
         return true;
      }
      if ("startup-deployments-per-target".equals(name))
      {
         DeploymentScenario s = scenarioOf(TestClasses.TwoTargets.class);
         r.list("startup.x", namesOf(s.getStartupDeploymentsFor(new TargetDescription("x"))));
         r.list("startup.y", namesOf(s.getStartupDeploymentsFor(new TargetDescription("y"))));
         r.list("startup.default", namesOf(s.getStartupDeploymentsFor(TargetDescription.DEFAULT)));
         return true;
      }
      if ("empty-scenario".equals(name))
      {
         final DeploymentScenario s = new DeploymentScenario();
         r.kv("deployments.size", Integer.valueOf(s.getDeployments().size()));
         r.kv("targets.size", Integer.valueOf(s.getTargets().size()));
         r.kv("protocols.size", Integer.valueOf(s.getProtocols().size()));
         r.kv("default", nameOf(s.getDeployment(DeploymentTargetDescription.DEFAULT)));
         r.attempt("addDeployment(null)", () -> s.addDeployment(null));
         r.attempt("getDeployments().add", () -> s.getDeployments().add(null));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // description value types
   // ------------------------------------------------------------------------

   private boolean modelFixture(Rec r)
   {
      if ("description-equality".equals(name))
      {
         r.kv("target.default.name", TargetDescription.DEFAULT.getName());
         r.kv("target.equalByName", Boolean.valueOf(
               new TargetDescription("a").equals(new TargetDescription("a"))));
         r.kv("target.hashEqual", Boolean.valueOf(
               new TargetDescription("a").hashCode() == new TargetDescription("a").hashCode()));
         r.kv("target.differByName", Boolean.valueOf(
               new TargetDescription("a").equals(new TargetDescription("b"))));
         r.kv("target.equalsDefault", Boolean.valueOf(
               new TargetDescription("_DEFAULT_").equals(TargetDescription.DEFAULT)));
         r.kv("target.equalsNull", Boolean.valueOf(new TargetDescription("a").equals(null)));
         r.kv("target.equalsOtherType", Boolean.valueOf(new TargetDescription("a").equals("a")));
         r.kv("protocol.default.name", ProtocolDescription.DEFAULT.getName());
         r.kv("protocol.equalByName", Boolean.valueOf(
               new ProtocolDescription("p").equals(new ProtocolDescription("p"))));
         r.kv("deploymentTarget.default.name", DeploymentTargetDescription.DEFAULT.getName());
         r.kv("deploymentTarget.equalByName", Boolean.valueOf(
               new DeploymentTargetDescription("d").equals(new DeploymentTargetDescription("d"))));
         return true;
      }
      if ("description-null-name".equals(name))
      {
         r.attempt("TargetDescription(null)", () -> new TargetDescription(null));
         r.attempt("ProtocolDescription(null)", () -> new ProtocolDescription(null));
         r.attempt("DeploymentTargetDescription(null)", () -> new DeploymentTargetDescription(null));
         return true;
      }
      if ("deployment-description-defaults".equals(name))
      {
         DeploymentDescription d = new DeploymentDescription(
               "d", TestClasses.jar("d.jar"));
         dumpDeployment(r, "deployment", d);
         return true;
      }
      if ("deployment-description-mutators".equals(name))
      {
         DeploymentDescription d = new DeploymentDescription("d", TestClasses.jar("d.jar"));
         d.setOrder(4).shouldBeManaged(false).shouldBeTestable(false)
               .setTarget(new TargetDescription("t"))
               .setProtocol(new ProtocolDescription("p"))
               .setExpectedException(IllegalArgumentException.class);
         dumpDeployment(r, "deployment", d);
         d.setTestableArchive(TestClasses.jar("testable.jar"));
         r.kv("testableArchive", archiveName(d.getTestableArchive()));
         return true;
      }
      if ("deployment-description-descriptor".equals(name))
      {
         DeploymentDescription d = new DeploymentDescription("d", TestClasses.descriptor());
         dumpDeployment(r, "deployment", d);
         return true;
      }
      if ("deployment-description-nulls".equals(name))
      {
         r.attempt("name-null-archive", () -> new DeploymentDescription(
               null, TestClasses.jar("n.jar")));
         r.attempt("null-archive", () -> new DeploymentDescription(
               "d", (Archive<?>) null));
         r.attempt("null-descriptor", () -> new DeploymentDescription(
               "d", (org.jboss.shrinkwrap.descriptor.api.Descriptor) null));
         return true;
      }
      if ("testable-archive-without-testable-flag".equals(name))
      {
         DeploymentDescription d = new DeploymentDescription("d", TestClasses.jar("d.jar"));
         d.shouldBeTestable(false);
         r.kv("testable.before", Boolean.valueOf(d.testable()));
         d.setTestableArchive(TestClasses.jar("t.jar"));
         r.kv("testable.after", Boolean.valueOf(d.testable()));
         r.kv("testableArchive", archiveName(d.getTestableArchive()));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // recording helpers
   // ------------------------------------------------------------------------

   private static DeploymentScenario scenarioOf(Class<?> target)
   {
      return new AnnotationDeploymentScenarioGenerator().generate(new TestClass(target));
   }

   private static void dumpScenario(Rec r, DeploymentScenario scenario)
   {
      List<DeploymentDescription> deployments =
            new ArrayList<DeploymentDescription>(scenario.getDeployments());
      Collections.sort(deployments, (a, b) -> {
         int byName = a.getName().compareTo(b.getName());
         return byName != 0 ? byName : archiveName(a.getArchive()).compareTo(archiveName(b.getArchive()));
      });
      r.kv("deployments.size", Integer.valueOf(deployments.size()));
      for (int i = 0; i < deployments.size(); i++)
      {
         dumpDeployment(r, "deployment[" + i + "]", deployments.get(i));
      }
      r.sorted("targets", names(scenario.getTargets()));
      r.sorted("protocols", protocolNames(scenario.getProtocols()));
   }

   private static void dumpDeployment(Rec r, String p, final DeploymentDescription d)
   {
      r.value(p + ".name", () -> d.getName());
      r.value(p + ".order", () -> Integer.valueOf(d.getOrder()));
      r.value(p + ".managed", () -> Boolean.valueOf(d.managed()));
      r.value(p + ".testable", () -> Boolean.valueOf(d.testable()));
      r.value(p + ".target", () -> d.getTarget() == null ? null : d.getTarget().getName());
      r.value(p + ".protocol", () -> d.getProtocol() == null ? null : d.getProtocol().getName());
      r.value(p + ".expectedException", () -> d.getExpectedException());
      r.value(p + ".isArchive", () -> Boolean.valueOf(d.isArchiveDeployment()));
      r.value(p + ".isDescriptor", () -> Boolean.valueOf(d.isDescriptorDeployment()));
      r.value(p + ".archive", () -> archiveName(d.getArchive()));
      r.value(p + ".descriptor", () -> d.getDescriptor() == null
            ? null : d.getDescriptor().getDescriptorName());
      r.value(p + ".testableArchive", () -> archiveName(d.getTestableArchive()));
      if (d.getArchive() != null)
      {
         r.sorted(p + ".content", contentOf(d.getArchive()));
      }
   }

   static List<String> contentOf(Archive<?> archive)
   {
      List<String> paths = new ArrayList<String>();
      for (ArchivePath path : archive.getContent().keySet())
      {
         paths.add(path.get());
      }
      return paths;
   }

   private static List<String> names(Set<TargetDescription> targets)
   {
      List<String> out = new ArrayList<String>();
      for (TargetDescription t : targets)
      {
         out.add(t.getName());
      }
      return out;
   }

   private static List<String> protocolNames(Set<ProtocolDescription> protocols)
   {
      List<String> out = new ArrayList<String>();
      for (ProtocolDescription p : protocols)
      {
         out.add(p.getName());
      }
      return out;
   }

   private static List<String> namesOf(List<DeploymentDescription> deployments)
   {
      List<String> out = new ArrayList<String>();
      for (DeploymentDescription d : deployments)
      {
         out.add(d.getName());
      }
      return out;
   }

   private static List<String> archiveNamesOf(List<DeploymentDescription> deployments)
   {
      List<String> out = new ArrayList<String>();
      for (DeploymentDescription d : deployments)
      {
         out.add(archiveNameOf(d));
      }
      return out;
   }

   private static String nameOf(DeploymentDescription d)
   {
      return d == null ? "<null>" : d.getName();
   }

   private static String archiveNameOf(DeploymentDescription d)
   {
      return d == null ? "<null>" : archiveName(d.getArchive());
   }

   static String archiveName(Archive<?> archive)
   {
      return archive == null ? "<null>" : archive.getName();
   }
}
