package arqprobe.fam;

import java.io.File;
import java.net.URL;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.domain.Stubs;

import org.jboss.arquillian.impl.MapObject;
import org.jboss.arquillian.impl.configuration.api.ArquillianDescriptor;
import org.jboss.arquillian.impl.configuration.api.ContainerDef;
import org.jboss.arquillian.impl.domain.Container;
import org.jboss.arquillian.impl.domain.ContainerRegistry;
import org.jboss.arquillian.impl.domain.FilteredURLClassLoader;
import org.jboss.arquillian.impl.domain.ProtocolDefinition;
import org.jboss.arquillian.impl.domain.ProtocolRegistry;
import org.jboss.arquillian.spi.ServiceLoader;
import org.jboss.arquillian.spi.client.container.ContainerConfiguration;
import org.jboss.arquillian.spi.client.container.DeployableContainer;
import org.jboss.arquillian.spi.client.protocol.ProtocolDescription;
import org.jboss.arquillian.spi.client.test.DeploymentTargetDescription;
import org.jboss.arquillian.spi.client.test.TargetDescription;
import org.jboss.shrinkwrap.descriptor.api.Descriptors;

/**
 * The container domain model: the two registries, what they consider a match,
 * and how a declaration in {@code arquillian.xml} becomes a live configuration
 * object.
 *
 * Three things here are worth more than their line count. The registries answer
 * a "default" lookup by a rule that is not written down anywhere — one entry
 * wins by being the only entry, several entries need a flag — and both
 * registries implement that rule differently. The populator decides what counts
 * as a settable property by matching method shapes, so which declarations it
 * silently ignores is an observable. And a configuration class is instantiated
 * with {@code Class.newInstance()}, whose failure modes differ from those of
 * the replacement a later step installs; those fixtures are recorded so the
 * difference is visible rather than surprising.
 *
 * Code-driven family — the fixture name selects the scenario.
 */
public class DomainFixture implements Fixture
{
   private final String name;

   public DomainFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Exception
   {
      if (registry(r) || container(r) || populate(r) || protocolDefinition(r)
            || protocolRegistry(r) || descriptions(r) || classLoader(r))
      {
         return;
      }
      throw new IllegalArgumentException("unknown domain fixture: " + name);
   }

   // ---------------------------------------------------------- ContainerRegistry

   private boolean registry(Rec r) throws Exception
   {
      if ("reg-empty".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         r.value("containers.size", () -> Integer.valueOf(reg.getContainers().size()));
         r.value("byName", () -> reg.getContainer("nothing"));
         r.value("byTarget", () -> reg.getContainer(new TargetDescription("nothing")));
         r.value("byDefaultTarget", () -> reg.getContainer(TargetDescription.DEFAULT));
         return true;
      }
      if ("reg-create-one".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         Container c = reg.create(containerDef("alpha"), oneContainer());
         r.kv("created.name", c.getName());
         r.value("containers.size", () -> Integer.valueOf(reg.getContainers().size()));
         r.value("byName", () -> named(reg.getContainer("alpha")));
         r.value("byName.other", () -> named(reg.getContainer("beta")));
         r.value("byTarget", () -> named(reg.getContainer(new TargetDescription("alpha"))));
         // One entry answers a default lookup without being marked default.
         r.value("byDefaultTarget", () -> named(reg.getContainer(TargetDescription.DEFAULT)));
         return true;
      }
      if ("reg-create-two-no-default".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         reg.create(containerDef("alpha"), oneContainer());
         reg.create(containerDef("beta"), oneContainer());
         r.value("containers.size", () -> Integer.valueOf(reg.getContainers().size()));
         r.value("byName.alpha", () -> named(reg.getContainer("alpha")));
         r.value("byName.beta", () -> named(reg.getContainer("beta")));
         r.value("byDefaultTarget", () -> named(reg.getContainer(TargetDescription.DEFAULT)));
         return true;
      }
      if ("reg-create-two-one-default".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         reg.create(containerDef("alpha"), oneContainer());
         reg.create(containerDef("beta").setDefault(), oneContainer());
         r.value("byDefaultTarget", () -> named(reg.getContainer(TargetDescription.DEFAULT)));
         return true;
      }
      if ("reg-create-two-both-default".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         reg.create(containerDef("alpha").setDefault(), oneContainer());
         reg.create(containerDef("beta").setDefault(), oneContainer());
         // Both are marked; the registry scans in insertion order and stops.
         r.value("byDefaultTarget", () -> named(reg.getContainer(TargetDescription.DEFAULT)));
         return true;
      }
      if ("reg-create-duplicate-names".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         reg.create(containerDef("same"), oneContainer());
         reg.create(containerDef("same"), oneContainer());
         r.value("containers.size", () -> Integer.valueOf(reg.getContainers().size()));
         // Insertion order is the registry's own list order, which is a contract.
         r.valueList("names", () -> names(reg.getContainers()));
         r.value("byName", () -> named(reg.getContainer("same")));
         return true;
      }
      if ("reg-create-null-definition".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         r.attempt("create", () -> reg.create(null, oneContainer()));
         return true;
      }
      if ("reg-create-null-loader".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         r.attempt("create", () -> reg.create(containerDef("alpha"), null));
         return true;
      }
      if ("reg-create-no-container-service".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         r.attempt("create", () -> reg.create(containerDef("alpha"), new Stubs.FixedServiceLoader()));
         r.value("containers.size", () -> Integer.valueOf(reg.getContainers().size()));
         return true;
      }
      if ("reg-create-two-container-services".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         ServiceLoader loader = new Stubs.FixedServiceLoader(
               new Stubs.TestContainer(), new Stubs.TestContainer());
         r.attempt("create", () -> reg.create(containerDef("alpha"), loader));
         r.value("containers.size", () -> Integer.valueOf(reg.getContainers().size()));
         return true;
      }
      if ("reg-get-container-null-target".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         r.attempt("byTarget", () -> reg.getContainer((TargetDescription) null));
         // The name overload has no such check.
         r.value("byName", () -> reg.getContainer((String) null));
         return true;
      }
      if ("reg-containers-unmodifiable".equals(name))
      {
         ContainerRegistry reg = new ContainerRegistry();
         reg.create(containerDef("alpha"), oneContainer());
         List<Container> view = reg.getContainers();
         r.attempt("add", () -> view.add(null));
         r.attempt("clear", () -> view.clear());
         r.value("size.after", () -> Integer.valueOf(reg.getContainers().size()));
         return true;
      }
      if ("reg-create-empty-name".equals(name))
      {
         // The definition's name is never validated by the registry; the
         // Container constructor only rejects null.
         ContainerRegistry reg = new ContainerRegistry();
         r.attempt("create", () -> reg.create(containerDef(""), oneContainer()));
         r.value("byName", () -> named(reg.getContainer("")));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------ Container

   private boolean container(Rec r) throws Exception
   {
      if ("container-null-name".equals(name))
      {
         r.attempt("ctor", () -> new Container(
               null, loader(), new Stubs.TestContainer(), containerDef("alpha")));
         return true;
      }
      if ("container-null-classloader".equals(name))
      {
         r.attempt("ctor", () -> new Container(
               "alpha", null, new Stubs.TestContainer(), containerDef("alpha")));
         return true;
      }
      if ("container-null-deployable".equals(name))
      {
         r.attempt("ctor", () -> new Container("alpha", loader(), null, containerDef("alpha")));
         return true;
      }
      if ("container-null-configuration".equals(name))
      {
         r.attempt("ctor", () -> new Container(
               "alpha", loader(), new Stubs.TestContainer(), null));
         return true;
      }
      if ("container-accessors".equals(name))
      {
         Container c = container("alpha", Stubs.Config.class, containerDef("alpha"));
         r.kv("name", c.getName());
         r.kv("deployableContainer", c.getDeployableContainer());
         r.kv("classLoader.same", Boolean.valueOf(c.getClassLoader() == loader()));
         r.kv("configuration.name", c.getContainerConfiguration().getContainerName());
         return true;
      }
      if ("container-create-config-all-types".equals(name))
      {
         ContainerDef def = containerDef("alpha")
               .property("anInt", "7")
               .property("anInteger", "8")
               .property("aLong", "9")
               .property("aLongObject", "10")
               .property("aDouble", "1.5")
               .property("aDoubleObject", "2.5")
               .property("aBoolean", "true")
               .property("aBooleanObject", "TRUE")
               .property("aString", "hello");
         Container c = container("alpha", Stubs.Config.class, def);
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-empty".equals(name))
      {
         Container c = container("alpha", Stubs.Config.class, containerDef("alpha"));
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-unknown-property".equals(name))
      {
         // Nothing on the configuration class answers to this name, and the
         // populator walks methods rather than the map, so it is dropped in
         // silence rather than reported.
         ContainerDef def = containerDef("alpha").property("noSuchProperty", "x");
         Container c = container("alpha", Stubs.Config.class, def);
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-rejected-shapes".equals(name))
      {
         // A chained setter (returns a value), a two-argument setter and a
         // setter whose fourth character is lower case: all three are ignored.
         ContainerDef def = containerDef("alpha")
               .property("chained", "a")
               .property("pair", "b")
               .property("lowercase", "c");
         Container c = container("alpha", Stubs.Config.class, def);
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-bad-number".equals(name))
      {
         ContainerDef def = containerDef("alpha").property("anInt", "not-a-number");
         Container c = container("alpha", Stubs.Config.class, def);
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-bad-boolean".equals(name))
      {
         // Boolean.valueOf takes anything and means false; no failure here.
         ContainerDef def = containerDef("alpha").property("aBoolean", "yes");
         Container c = container("alpha", Stubs.Config.class, def);
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-unconvertible-type".equals(name))
      {
         // No conversion rule covers File, so the String is passed through and
         // the reflective call refuses it.
         ContainerDef def = containerDef("alpha").property("aFile", "/probe-fixture/x");
         Container c = container("alpha", Stubs.Config.class, def);
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-setter-throws".equals(name))
      {
         ContainerDef def = containerDef("alpha").property("boom", "x");
         Container c = container("alpha", Stubs.ThrowingSetterConfig.class, def);
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-validate-throws".equals(name))
      {
         Container c = container("alpha", Stubs.InvalidConfig.class, containerDef("alpha"));
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-no-public-ctor".equals(name))
      {
         // Type only: the sentence a failed access check produces is the JVM's
         // and was reworded after JDK 8. What this fixture is asking is whether
         // an unreachable constructor fails at all — a migration that reaches
         // for setAccessible to "fix" it would turn this into a success.
         Container c = container("alpha", Stubs.NoPublicCtorConfig.class, containerDef("alpha"));
         r.valueTypeOnly("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-no-noarg-ctor".equals(name))
      {
         Container c = container("alpha", Stubs.NoNoArgCtorConfig.class, containerDef("alpha"));
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-ctor-throws".equals(name))
      {
         Container c = container("alpha", Stubs.ThrowingCtorConfig.class, containerDef("alpha"));
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-ctor-throws-checked".equals(name))
      {
         Container c = container("alpha", Stubs.CheckedCtorConfig.class, containerDef("alpha"));
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-abstract".equals(name))
      {
         Container c = container("alpha", Stubs.AbstractConfig.class, containerDef("alpha"));
         r.value("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-create-config-hidden-class".equals(name))
      {
         // The class is package-private with a public constructor. The populator
         // would reach it, but the instantiation never gets that far: the access
         // check is on the class as well. Type only, for the same reason as the
         // fixture above.
         ContainerDef def = containerDef("alpha").property("aString", "reached");
         Container c = container("alpha", Stubs.hiddenConfigClass(), def);
         r.valueTypeOnly("config", () -> c.createDeployableConfiguration());
         return true;
      }
      if ("container-protocol-configuration".equals(name))
      {
         ContainerDef def = containerDef("alpha");
         def.protocol("Servlet 3.0").property("host", "example");
         def.protocol("jmx").property("port", "1234");
         Container c = container("alpha", Stubs.Config.class, def);
         r.value("has.servlet", () -> Boolean.valueOf(
               c.hasProtocolConfiguration(new ProtocolDescription("Servlet 3.0"))));
         r.value("has.jmx", () -> Boolean.valueOf(
               c.hasProtocolConfiguration(new ProtocolDescription("jmx"))));
         r.value("has.missing", () -> Boolean.valueOf(
               c.hasProtocolConfiguration(new ProtocolDescription("nope"))));
         r.value("has.default", () -> Boolean.valueOf(
               c.hasProtocolConfiguration(ProtocolDescription.DEFAULT)));
         r.value("get.servlet", () -> typeOf(
               c.getProtocolConfiguration(new ProtocolDescription("Servlet 3.0"))));
         r.value("get.missing", () -> typeOf(
               c.getProtocolConfiguration(new ProtocolDescription("nope"))));
         return true;
      }
      if ("container-protocol-configuration-none".equals(name))
      {
         Container c = container("alpha", Stubs.Config.class, containerDef("alpha"));
         r.value("has", () -> Boolean.valueOf(
               c.hasProtocolConfiguration(new ProtocolDescription("Servlet 3.0"))));
         r.value("get", () -> typeOf(
               c.getProtocolConfiguration(new ProtocolDescription("Servlet 3.0"))));
         return true;
      }
      if ("container-protocol-configuration-null".equals(name))
      {
         // No protocols are declared, so the loop that would dereference the
         // argument never runs and the null is never noticed.
         Container c = container("alpha", Stubs.Config.class, containerDef("alpha"));
         r.value("has.noProtocols", () -> Boolean.valueOf(c.hasProtocolConfiguration(null)));
         r.value("get.noProtocols", () -> typeOf(c.getProtocolConfiguration(null)));

         ContainerDef def = containerDef("beta");
         def.protocol("jmx");
         Container withProtocol = container("beta", Stubs.Config.class, def);
         r.value("has.oneProtocol", () -> Boolean.valueOf(
               withProtocol.hasProtocolConfiguration(null)));
         r.value("get.oneProtocol", () -> typeOf(withProtocol.getProtocolConfiguration(null)));
         return true;
      }
      if ("container-protocol-configuration-duplicate-type".equals(name))
      {
         // Two declarations of one protocol type; the lookup scans in document
         // order and returns the first, which is the declared contract.
         ContainerDef def = containerDef("alpha");
         def.protocol("jmx").property("port", "1");
         def.protocol("jmx").property("port", "2");
         Container c = container("alpha", Stubs.Config.class, def);
         r.value("protocols.size", () -> Integer.valueOf(
               c.getContainerConfiguration().getProtocols().size()));
         r.value("get.port", () -> c.getProtocolConfiguration(new ProtocolDescription("jmx"))
               .getProtocolProperties().get("port"));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------ MapObject

   private boolean populate(Rec r) throws Exception
   {
      if ("map-populate-types".equals(name))
      {
         Map<String, String> values = new LinkedHashMap<String, String>();
         values.put("anInt", "-1");
         values.put("anInteger", "-2");
         values.put("aLong", "-3");
         values.put("aLongObject", "-4");
         values.put("aDouble", "-0.25");
         values.put("aDoubleObject", "-0.5");
         values.put("aBoolean", "TrUe");
         values.put("aBooleanObject", "false");
         values.put("aString", " padded ");
         Stubs.Config config = new Stubs.Config();
         r.attempt("populate", () -> MapObject.populate(config, values));
         r.kv("config", config);
         return true;
      }
      if ("map-populate-empty".equals(name))
      {
         Stubs.Config config = new Stubs.Config();
         r.attempt("populate", () -> MapObject.populate(config, new HashMap<String, String>()));
         r.kv("config", config);
         return true;
      }
      if ("map-populate-null-map".equals(name))
      {
         Stubs.Config config = new Stubs.Config();
         r.attempt("populate", () -> MapObject.populate(config, null));
         r.kv("config", config);
         return true;
      }
      if ("map-populate-null-object".equals(name))
      {
         r.attempt("populate", () -> MapObject.populate(null, new HashMap<String, String>()));
         return true;
      }
      if ("map-populate-null-value".equals(name))
      {
         // The key is present, so the setter is called; the conversion of a
         // null value is what the fixture is for.
         Map<String, String> values = new HashMap<String, String>();
         values.put("aString", null);
         Stubs.Config config = new Stubs.Config();
         r.attempt("populate", () -> MapObject.populate(config, values));
         r.kv("config", config);
         return true;
      }
      if ("map-populate-null-number".equals(name))
      {
         // Type only: the text belongs to Integer.parseInt, which reworded it
         // after JDK 11. That a null value for a numeric property fails at all
         // is the observable.
         Map<String, String> values = new HashMap<String, String>();
         values.put("anInt", null);
         Stubs.Config config = new Stubs.Config();
         r.valueTypeOnly("populate", () -> {
            MapObject.populate(config, values);
            return "ok";
         });
         r.kv("config", config);
         return true;
      }
      if ("map-populate-hidden-class".equals(name))
      {
         Map<String, String> values = new HashMap<String, String>();
         values.put("aString", "reached");
         ContainerConfiguration config = Stubs.hiddenConfig();
         r.attempt("populate", () -> MapObject.populate(config, values));
         r.kv("config", config);
         return true;
      }
      if ("map-convert-files".equals(name))
      {
         // Characters a URI has to escape, all of them ASCII: what a non-ASCII
         // name would record is the JDK's choice of path encoding, not this
         // codebase's conversion, and that choice has moved between releases.
         File[] files = new File[] {
               new File("/probe-fixture/a.jar"),
               new File("/probe-fixture/dir with space/b.jar"),
               new File("/probe-fixture/a#b?c.jar"),
         };
         r.value("urls", () -> MapObject.convert(files));
         return true;
      }
      if ("map-convert-files-empty".equals(name))
      {
         r.value("urls.length", () -> Integer.valueOf(MapObject.convert(new File[0]).length));
         return true;
      }
      if ("map-convert-files-null".equals(name))
      {
         r.attempt("convert", () -> MapObject.convert(null));
         return true;
      }
      if ("map-convert-files-null-element".equals(name))
      {
         r.attempt("convert", () -> MapObject.convert(new File[] {null}));
         return true;
      }
      return false;
   }

   // ---------------------------------------------------------- ProtocolDefinition

   private boolean protocolDefinition(Rec r) throws Exception
   {
      if ("protodef-null-protocol".equals(name))
      {
         r.attempt("ctor", () -> new ProtocolDefinition(null));
         return true;
      }
      if ("protodef-null-configuration".equals(name))
      {
         r.attempt("ctor", () -> new ProtocolDefinition(new Stubs.TestProtocol("jmx"), null));
         return true;
      }
      if ("protodef-defaults".equals(name))
      {
         ProtocolDefinition d = new ProtocolDefinition(new Stubs.TestProtocol("jmx"));
         r.kv("name", d.getName());
         r.kv("description", d.getProtocolDescription().getName());
         r.kv("default", Boolean.valueOf(d.isDefaultProtocol()));
         r.valueMap("configuration", () -> d.getProtocolConfiguration());
         r.kv("protocol", d.getProtocol());
         return true;
      }
      if ("protodef-marked-default".equals(name))
      {
         ProtocolDefinition d = new ProtocolDefinition(
               new Stubs.TestProtocol("jmx"), new HashMap<String, String>(), true);
         r.kv("default", Boolean.valueOf(d.isDefaultProtocol()));
         return true;
      }
      if ("protodef-create-configuration".equals(name))
      {
         Map<String, String> config = new LinkedHashMap<String, String>();
         config.put("host", "example");
         config.put("port", "8080");
         ProtocolDefinition d = new ProtocolDefinition(new Stubs.TestProtocol("jmx"), config);
         r.value("created", () -> d.createProtocolConfiguration());
         return true;
      }
      if ("protodef-create-configuration-override".equals(name))
      {
         Map<String, String> config = new HashMap<String, String>();
         config.put("host", "declared");
         ProtocolDefinition d = new ProtocolDefinition(new Stubs.TestProtocol("jmx"), config);
         Map<String, String> other = new HashMap<String, String>();
         other.put("host", "supplied");
         r.value("created", () -> d.createProtocolConfiguration(other));
         r.valueMap("declared.unchanged", () -> d.getProtocolConfiguration());
         return true;
      }
      if ("protodef-create-configuration-null-map".equals(name))
      {
         ProtocolDefinition d = new ProtocolDefinition(new Stubs.TestProtocol("jmx"));
         r.value("created", () -> d.createProtocolConfiguration(null));
         return true;
      }
      if ("protodef-create-configuration-no-public-ctor".equals(name))
      {
         ProtocolDefinition d = new ProtocolDefinition(
               new Stubs.TestProtocol("jmx", Stubs.NoPublicCtorProtoConfig.class));
         r.valueTypeOnly("created", () -> d.createProtocolConfiguration());
         return true;
      }
      if ("protodef-configuration-map-is-live".equals(name))
      {
         // The map handed in is kept, not copied; a later write to it is visible
         // through the definition.
         Map<String, String> config = new HashMap<String, String>();
         ProtocolDefinition d = new ProtocolDefinition(new Stubs.TestProtocol("jmx"), config);
         config.put("host", "written-later");
         r.valueMap("configuration", () -> d.getProtocolConfiguration());
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------ ProtocolRegistry

   private boolean protocolRegistry(Rec r) throws Exception
   {
      if ("protoreg-empty".equals(name))
      {
         ProtocolRegistry reg = new ProtocolRegistry();
         r.value("specific", () -> nameOf(reg.getProtocol(new ProtocolDescription("jmx"))));
         r.value("default", () -> nameOf(reg.getProtocol(ProtocolDescription.DEFAULT)));
         return true;
      }
      if ("protoreg-add-null".equals(name))
      {
         ProtocolRegistry reg = new ProtocolRegistry();
         r.attempt("add", () -> reg.addProtocol(null));
         return true;
      }
      if ("protoreg-get-null".equals(name))
      {
         ProtocolRegistry reg = new ProtocolRegistry();
         r.attempt("get", () -> reg.getProtocol(null));
         return true;
      }
      if ("protoreg-one-not-marked-default".equals(name))
      {
         ProtocolRegistry reg = new ProtocolRegistry();
         r.value("add.returnsSame", () -> Boolean.valueOf(
               reg.addProtocol(new ProtocolDefinition(new Stubs.TestProtocol("jmx"))) == reg));
         r.value("specific", () -> nameOf(reg.getProtocol(new ProtocolDescription("jmx"))));
         // A sole entry answers the default lookup, same rule as the containers.
         r.value("default", () -> nameOf(reg.getProtocol(ProtocolDescription.DEFAULT)));
         return true;
      }
      if ("protoreg-two-none-marked-default".equals(name))
      {
         ProtocolRegistry reg = new ProtocolRegistry();
         reg.addProtocol(new ProtocolDefinition(new Stubs.TestProtocol("jmx")));
         reg.addProtocol(new ProtocolDefinition(new Stubs.TestProtocol("servlet")));
         r.value("default", () -> nameOf(reg.getProtocol(ProtocolDescription.DEFAULT)));
         r.value("specific.jmx", () -> nameOf(reg.getProtocol(new ProtocolDescription("jmx"))));
         r.value("specific.missing", () -> nameOf(reg.getProtocol(new ProtocolDescription("nope"))));
         return true;
      }
      if ("protoreg-two-one-marked-default".equals(name))
      {
         ProtocolRegistry reg = new ProtocolRegistry();
         reg.addProtocol(new ProtocolDefinition(new Stubs.TestProtocol("jmx")));
         reg.addProtocol(new ProtocolDefinition(
               new Stubs.TestProtocol("servlet"), new HashMap<String, String>(), true));
         r.value("default", () -> nameOf(reg.getProtocol(ProtocolDescription.DEFAULT)));
         return true;
      }
      if ("protoreg-two-both-marked-default".equals(name))
      {
         ProtocolRegistry reg = new ProtocolRegistry();
         reg.addProtocol(new ProtocolDefinition(
               new Stubs.TestProtocol("jmx"), new HashMap<String, String>(), true));
         reg.addProtocol(new ProtocolDefinition(
               new Stubs.TestProtocol("servlet"), new HashMap<String, String>(), true));
         r.value("default", () -> nameOf(reg.getProtocol(ProtocolDescription.DEFAULT)));
         return true;
      }
      if ("protoreg-duplicate-description".equals(name))
      {
         ProtocolRegistry reg = new ProtocolRegistry();
         reg.addProtocol(new ProtocolDefinition(new Stubs.TestProtocol("jmx")));
         r.attempt("add.again", () -> reg.addProtocol(
               new ProtocolDefinition(new Stubs.TestProtocol("jmx"))));
         r.value("specific", () -> nameOf(reg.getProtocol(new ProtocolDescription("jmx"))));
         return true;
      }
      if ("protoreg-default-description-registered".equals(name))
      {
         // A protocol that calls itself "_DEFAULT_" is reachable only through
         // the default lookup, which does not look at names at all.
         ProtocolRegistry reg = new ProtocolRegistry();
         reg.addProtocol(new ProtocolDefinition(new Stubs.TestProtocol("_DEFAULT_")));
         reg.addProtocol(new ProtocolDefinition(new Stubs.TestProtocol("jmx")));
         r.value("default", () -> nameOf(reg.getProtocol(ProtocolDescription.DEFAULT)));
         r.value("byName", () -> nameOf(reg.getProtocol(new ProtocolDescription("_DEFAULT_"))));
         return true;
      }
      return false;
   }

   // --------------------------------------------------------- the description types

   private boolean descriptions(Rec r) throws Exception
   {
      if ("descr-protocol-null-name".equals(name))
      {
         r.attempt("ctor", () -> new ProtocolDescription(null));
         return true;
      }
      if ("descr-protocol-equality".equals(name))
      {
         ProtocolDescription a = new ProtocolDescription("jmx");
         r.kv("self", Boolean.valueOf(a.equals(a)));
         r.kv("equal", Boolean.valueOf(a.equals(new ProtocolDescription("jmx"))));
         r.kv("different", Boolean.valueOf(a.equals(new ProtocolDescription("servlet"))));
         r.kv("null", Boolean.valueOf(a.equals(null)));
         r.kv("otherType", Boolean.valueOf(a.equals("jmx")));
         r.kv("hash.equal", Boolean.valueOf(
               a.hashCode() == new ProtocolDescription("jmx").hashCode()));
         r.kv("default.name", ProtocolDescription.DEFAULT.getName());
         r.kv("equalsDefault", Boolean.valueOf(
               ProtocolDescription.DEFAULT.equals(new ProtocolDescription("_DEFAULT_"))));
         return true;
      }
      if ("descr-target-null-name".equals(name))
      {
         // Unlike the protocol description, this one takes a null name.
         TargetDescription t = new TargetDescription(null);
         r.kv("name", t.getName());
         r.kv("hash", Integer.valueOf(t.hashCode()));
         r.kv("equalsOther", Boolean.valueOf(t.equals(new TargetDescription(null))));
         r.kv("equalsNamed", Boolean.valueOf(t.equals(new TargetDescription("x"))));
         r.kv("namedEqualsThis", Boolean.valueOf(new TargetDescription("x").equals(t)));
         return true;
      }
      if ("descr-target-equality".equals(name))
      {
         TargetDescription a = new TargetDescription("alpha");
         r.kv("self", Boolean.valueOf(a.equals(a)));
         r.kv("equal", Boolean.valueOf(a.equals(new TargetDescription("alpha"))));
         r.kv("different", Boolean.valueOf(a.equals(new TargetDescription("beta"))));
         r.kv("null", Boolean.valueOf(a.equals(null)));
         r.kv("otherType", Boolean.valueOf(a.equals(new DeploymentTargetDescription("alpha"))));
         r.kv("default.name", TargetDescription.DEFAULT.getName());
         r.kv("equalsDefault", Boolean.valueOf(
               TargetDescription.DEFAULT.equals(new TargetDescription("_DEFAULT_"))));
         return true;
      }
      if ("descr-deployment-target-equality".equals(name))
      {
         DeploymentTargetDescription a = new DeploymentTargetDescription("alpha");
         r.kv("self", Boolean.valueOf(a.equals(a)));
         r.kv("equal", Boolean.valueOf(a.equals(new DeploymentTargetDescription("alpha"))));
         r.kv("different", Boolean.valueOf(a.equals(new DeploymentTargetDescription("beta"))));
         r.kv("null", Boolean.valueOf(a.equals(null)));
         r.kv("otherType", Boolean.valueOf(a.equals(new TargetDescription("alpha"))));
         r.kv("nullName.hash", Integer.valueOf(new DeploymentTargetDescription(null).hashCode()));
         r.kv("default.name", DeploymentTargetDescription.DEFAULT.getName());
         return true;
      }
      return false;
   }

   // ------------------------------------------------------- FilteredURLClassLoader

   private boolean classLoader(Rec r) throws Exception
   {
      if ("fcl-filtered-name-comes-from-parent".equals(name))
      {
         // The filter names what the parent owns, so a matching name skips the
         // local search entirely and the class is the one already loaded.
         FilteredURLClassLoader cl = filtered("arqprobe\\..*");
         r.value("loaded", () -> describeLoad(cl, Stubs.Marker.NAME));
         return true;
      }
      if ("fcl-unfiltered-name-is-loaded-locally".equals(name))
      {
         FilteredURLClassLoader cl = filtered("nothing\\.matches\\.this");
         r.value("loaded", () -> describeLoad(cl, Stubs.Marker.NAME));
         return true;
      }
      if ("fcl-unfiltered-name-not-found-locally".equals(name))
      {
         // Not in the local URLs and not matched by the filter: the parent is
         // still asked, because the local search only short-circuits on success.
         FilteredURLClassLoader cl = new FilteredURLClassLoader(new URL[0], "nothing\\.matches\\.this");
         r.value("loaded", () -> describeLoad(cl, "java.util.ArrayList"));
         return true;
      }
      if ("fcl-missing-class".equals(name))
      {
         FilteredURLClassLoader cl = filtered("nothing\\.matches\\.this");
         r.attempt("load", () -> cl.loadClass("no.such.Class", false));
         return true;
      }
      if ("fcl-null-name".equals(name))
      {
         FilteredURLClassLoader cl = filtered("arqprobe\\..*");
         r.attempt("load", () -> cl.loadClass(null, false));
         r.attempt("resource", () -> cl.getResource(null));
         return true;
      }
      if ("fcl-malformed-filter".equals(name))
      {
         FilteredURLClassLoader cl = filtered("arqprobe\\.[");
         r.attempt("load", () -> cl.loadClass("java.util.ArrayList", false));
         return true;
      }
      if ("fcl-null-filter".equals(name))
      {
         FilteredURLClassLoader cl = filtered(null);
         r.attempt("load", () -> cl.loadClass("java.util.ArrayList", false));
         return true;
      }
      if ("fcl-resource-uses-slash-names".equals(name))
      {
         // The filter is written against dotted class names, and resources are
         // asked for with slashes, so a filter that hides a package from the
         // class path does not hide that package's resources. Recorded as it is.
         FilteredURLClassLoader cl = filtered("arqprobe\\..*");
         r.value("dotted.matches", () -> Boolean.valueOf(Stubs.Marker.NAME.matches("arqprobe\\..*")));
         r.value("resource.found", () -> Boolean.valueOf(
               cl.getResource("arqprobe/fam/domain/Stubs$Marker.class") != null));
         r.value("resource.owner", () -> ownerOf(cl,
               cl.getResource("arqprobe/fam/domain/Stubs$Marker.class")));
         return true;
      }
      if ("fcl-resource-missing".equals(name))
      {
         FilteredURLClassLoader cl = filtered("arqprobe\\..*");
         r.value("resource", () -> cl.getResource("no/such/resource.txt"));
         return true;
      }
      if ("fcl-resource-only-parent-has-it".equals(name))
      {
         FilteredURLClassLoader cl = new FilteredURLClassLoader(new URL[0], "nothing\\.matches\\.this");
         r.value("found", () -> Boolean.valueOf(
               cl.getResource("arqprobe/fam/domain/Stubs.class") != null));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------- helpers

   private static ContainerDef containerDef(String name)
   {
      return Descriptors.create(ArquillianDescriptor.class).container(name);
   }

   private static ClassLoader loader()
   {
      return DomainFixture.class.getClassLoader();
   }

   private static ServiceLoader oneContainer()
   {
      return new Stubs.FixedServiceLoader(new Stubs.TestContainer());
   }

   private static Container container(String name,
         Class<? extends ContainerConfiguration> configClass, ContainerDef def)
   {
      DeployableContainer<?> deployable =
            new Stubs.TestContainer(configClass, new ProtocolDescription("stub-protocol"));
      return new Container(name, loader(), deployable, def);
   }

   private static String named(Container c)
   {
      return c == null ? "<null>" : c.getName();
   }

   private static List<String> names(List<Container> containers)
   {
      List<String> out = new ArrayList<String>();
      for (Container c : containers)
      {
         out.add(c.getName());
      }
      return out;
   }

   private static String nameOf(ProtocolDefinition d)
   {
      return d == null ? "<null>" : d.getName();
   }

   private static String typeOf(Object o)
   {
      return o == null ? "<null>" : o.getClass().getSimpleName();
   }

   /** The directory the probe's own classes were compiled into. */
   private static URL classesRoot() throws Exception
   {
      String path = "arqprobe/fam/domain/Stubs.class";
      String url = Stubs.class.getResource("/" + path).toString();
      return new URL(url.substring(0, url.length() - path.length()));
   }

   private static FilteredURLClassLoader filtered(String regex) throws Exception
   {
      return new FilteredURLClassLoader(new URL[] {classesRoot()}, regex);
   }

   /**
    * Which loader ended up owning a class.
    *
    * The identity matters more than the name here: a class the filter sent to
    * the parent is the very one the fixture is already holding, and a class
    * found locally is a second, unrelated copy with the same name.
    */
   private static String describeLoad(ClassLoader cl, String name) throws Exception
   {
      Class<?> c = cl.loadClass(name);
      boolean local = c.getClassLoader() == cl;
      boolean sameAsOurs = c == classFor(name);
      return c.getName() + " local=" + local + " sameClassObject=" + sameAsOurs;
   }

   private static Class<?> classFor(String name) throws Exception
   {
      return Class.forName(name, false, loader());
   }

   private static String ownerOf(FilteredURLClassLoader cl, URL resource) throws Exception
   {
      if (resource == null)
      {
         return "<null>";
      }
      return resource.toString().startsWith(classesRoot().toString()) ? "local" : "elsewhere";
   }
}
