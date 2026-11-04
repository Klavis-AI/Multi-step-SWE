package arqprobe.fam;

import java.io.File;
import java.lang.reflect.AnnotatedElement;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

import javax.management.MBeanServer;
import javax.management.MBeanServerFactory;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.enrich.Beans;
import arqprobe.fam.enrich.Cdi;
import arqprobe.fam.enrich.Ejbs;
import arqprobe.fam.enrich.Jndi;
import arqprobe.fam.enrich.Log;

import org.jboss.arquillian.spi.TestDeployment;
import org.jboss.arquillian.spi.TestEnricher;
import org.jboss.arquillian.testenricher.cdi.BeansXMLProtocolProcessor;
import org.jboss.arquillian.testenricher.cdi.CDIInjectionEnricher;
import org.jboss.arquillian.testenricher.ejb.EJBInjectionEnricher;
import org.jboss.arquillian.testenricher.osgi.OSGiTestEnricher;
import org.jboss.arquillian.testenricher.resource.ResourceInjectionEnricher;
import org.jboss.shrinkwrap.api.Archive;
import org.jboss.shrinkwrap.api.ArchivePath;
import org.jboss.shrinkwrap.api.ArchivePaths;
import org.jboss.shrinkwrap.api.ShrinkWrap;
import org.jboss.shrinkwrap.api.asset.ByteArrayAsset;
import org.jboss.shrinkwrap.api.exporter.ZipExporter;
import org.jboss.shrinkwrap.api.asset.EmptyAsset;
import org.jboss.shrinkwrap.api.spec.EnterpriseArchive;
import org.jboss.shrinkwrap.api.spec.JavaArchive;
import org.jboss.shrinkwrap.api.spec.WebArchive;

/**
 * The four enrichers, driven through the SPI the container calls them on.
 *
 * {@code TestEnricher} is the extension point a container adapter implements,
 * and it is two methods: {@code enrich(Object)} fills in a test instance's
 * fields, {@code resolve(Method)} produces its arguments. Neither returns
 * anything about what it did — the field injection is a side effect on an
 * object the caller already holds, and the argument array says what was
 * produced but not how. So every fixture here hands the enricher an environment
 * it can watch: a JNDI provider ({@link Jndi}) that logs every name asked for,
 * a bean manager ({@link Cdi}) that logs every call and the injection point it
 * was handed, and bean classes ({@link Beans}) whose {@code toString} reports
 * whether a field was actually written.
 *
 * Three of these enrichers name an enterprise API in a string constant —
 * {@code "javax.annotation.Resource"}, {@code "javax.ejb.EJB"},
 * {@code "javax.inject.Inject"} — and load it from the thread context class
 * loader before doing anything. The step that moves the tree to Jakarta has to
 * move those constants too, and the fixtures have to move with them or they
 * would stop finding any annotated field at all. That is what the namespace
 * binding in {@code run.sh} is for: this family's source says {@code javax.*},
 * and is rewritten to {@code jakarta.*} when the tree under test has moved.
 *
 * Code-driven family — the fixture name selects the scenario.
 */
public class EnrichFixture implements Fixture
{
   /** The prefix {@code ResourceInjectionEnricher} puts in front of every name it builds. */
   private static final String RES = "java:/comp/env";

   private static final String BM_STANDARD = "java:comp/BeanManager";
   private static final String BM_SERVLET = "java:comp/env/BeanManager";
   private static final String BM_JBOSSAS = "java:global/test/arquillian-protocol/BeanManager";

   private final String name;

   public EnrichFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      Log.reset();
      String previous = Jndi.install();
      boolean known;
      try
      {
         known = resource(r) || ejb(r) || cdi(r) || beansxml(r) || osgi(r);
      }
      finally
      {
         Jndi.uninstall(previous);
         r.list("log", Log.lines());
      }
      if (!known)
      {
         throw new IllegalArgumentException("unknown enrich fixture: " + name);
      }
   }

   // ------------------------------------------------------------- @Resource

   /**
    * Fields and setters carrying {@code @Resource}.
    *
    * The enricher asks two questions of a field before it touches it — what
    * name does this resolve to, and does it already have a value — and the
    * second one is answered differently for primitives, which cannot be null.
    * The primitive block below is that difference, one width at a time.
    */
   private boolean resource(Rec r) throws Exception
   {
      if ("res-plain".equals(name))
      {
         // No attributes at all: the name is built from the declaring class and
         // the field, which is the only shape where the enricher composes a name
         // rather than being handed one.
         bindField(Beans.ResPlain.class, "value", "resource-plain");
         return enrich(r, resource(), new Beans.ResPlain());
      }
      if ("res-plain-not-found".equals(name))
      {
         return enrich(r, resource(), new Beans.ResPlain());
      }
      if ("res-named".equals(name))
      {
         Jndi.bind(RES + "/alpha", "resource-named");
         return enrich(r, resource(), new Beans.ResNamed());
      }
      if ("res-mapped".equals(name))
      {
         Jndi.bind("mapped/alpha", "resource-mapped");
         return enrich(r, resource(), new Beans.ResMapped());
      }
      if ("res-named-and-mapped".equals(name))
      {
         // Both are bound, so the transcript says which one was asked for
         // rather than only which one answered.
         Jndi.bind(RES + "/alpha", "named-loses");
         Jndi.bind("mapped/alpha", "mapped-wins");
         return enrich(r, resource(), new Beans.ResNamedAndMapped());
      }
      if ("res-preset".equals(name))
      {
         Jndi.bind(RES + "/" + Beans.ResPreset.class.getName() + "/value", "resource-preset");
         return enrich(r, resource(), new Beans.ResPreset());
      }
      if ("res-none".equals(name))
      {
         return enrich(r, resource(), new Beans.ResNone());
      }
      if ("res-inherited".equals(name))
      {
         // The field walk climbs past the instance's own class, and the name it
         // builds names the class that declared the field, not the one being
         // enriched.
         Jndi.bind("mapped/inherited", "resource-inherited");
         return enrich(r, resource(), new Beans.ResSub());
      }
      if ("res-wrong-type".equals(name))
      {
         Jndi.bind("mapped/alpha", "resource-mapped");
         return enrich(r, resource(), new Beans.ResWrongType());
      }
      if ("res-lookup-throws".equals(name))
      {
         // A lookup failure is not caught anywhere on this path, unlike the EJB
         // enricher's, so it comes back out wrapped.
         Jndi.failWith("mapped/alpha", new IllegalStateException("the directory said no"));
         return enrich(r, resource(), new Beans.ResMapped());
      }

      // ---- primitives: "already set" guessed from the value's string form ----

      if ("res-int-zero".equals(name))
      {
         Jndi.bind("mapped/int", Integer.valueOf(42));
         return enrich(r, resource(), new Beans.ResIntZero());
      }
      if ("res-int-set".equals(name))
      {
         Jndi.bind("mapped/int", Integer.valueOf(42));
         return enrich(r, resource(), new Beans.ResIntSet());
      }
      if ("res-long-zero".equals(name))
      {
         Jndi.bind("mapped/long", Long.valueOf(42L));
         return enrich(r, resource(), new Beans.ResLongZero());
      }
      if ("res-short-zero".equals(name))
      {
         Jndi.bind("mapped/short", Short.valueOf((short) 42));
         return enrich(r, resource(), new Beans.ResShortZero());
      }
      if ("res-double-zero".equals(name))
      {
         Jndi.bind("mapped/double", Double.valueOf(1.5d));
         return enrich(r, resource(), new Beans.ResDoubleZero());
      }
      if ("res-float-zero".equals(name))
      {
         Jndi.bind("mapped/float", Float.valueOf(1.5f));
         return enrich(r, resource(), new Beans.ResFloatZero());
      }
      if ("res-boolean-false".equals(name))
      {
         Jndi.bind("mapped/boolean", Boolean.TRUE);
         return enrich(r, resource(), new Beans.ResBooleanFalse());
      }
      if ("res-boolean-true".equals(name))
      {
         Jndi.bind("mapped/boolean", Boolean.TRUE);
         return enrich(r, resource(), new Beans.ResBooleanTrue());
      }
      if ("res-char-zero".equals(name))
      {
         // String.valueOf((char) 0) is a NUL, not "0", so the first test misses
         // this and a Character-specific one catches it.
         Jndi.bind("mapped/char", Character.valueOf('z'));
         return enrich(r, resource(), new Beans.ResCharZero());
      }
      if ("res-char-set".equals(name))
      {
         Jndi.bind("mapped/char", Character.valueOf('z'));
         return enrich(r, resource(), new Beans.ResCharSet());
      }

      // ---- setters ----

      if ("res-setter".equals(name))
      {
         Jndi.bind("mapped/setter", "resource-setter");
         return enrich(r, resource(), new Beans.ResSetter());
      }
      if ("res-setter-unnamed".equals(name))
      {
         // Neither name nor mappedName on a method: the enricher has nothing to
         // fall back on and looks up a null.
         return enrich(r, resource(), new Beans.ResSetterUnnamed());
      }
      if ("res-setter-two-args".equals(name))
      {
         Jndi.bind("mapped/setter", "resource-setter");
         return enrich(r, resource(), new Beans.ResSetterTwoArgs());
      }
      if ("res-not-a-setter".equals(name))
      {
         Jndi.bind("mapped/setter", "resource-setter");
         return enrich(r, resource(), new Beans.ResNotASetter());
      }
      if ("res-setter-throws".equals(name))
      {
         Jndi.bind("mapped/setter", "resource-setter");
         return enrich(r, resource(), new Beans.ResSetterThrows());
      }

      if ("res-resolve".equals(name))
      {
         // This enricher never supplies arguments; it declares the width of the
         // array and leaves every slot for someone else.
         return resolve(r, resource(), "two");
      }

      // ---- the two-arm dispatch the SPI does not reach ----
      //
      // resolveResource() asks two questions about one operand and the SPI
      // calls it only with a Field or a Method, so the two answers it gives to
      // anything else are unrecorded. They are the two a rewrite of that
      // dispatch is most likely to move, and they move in opposite directions,
      // so they are recorded here rather than argued about.

      if ("res-resolve-null-element".equals(name))
      {
         // Nothing between the call and the first question, so the first
         // question is what null meets.
         return resolveElement(r, null);
      }
      if ("res-resolve-other-element".equals(name))
      {
         // An AnnotatedElement that is neither: a Class is one, and both
         // questions answer no.
         return resolveElement(r, Beans.ResPlain.class);
      }
      return false;
   }

   /**
    * {@code resolveResource} is protected, so a subclass is the interface.
    *
    * Not a reflective {@code setAccessible} call: this family already proves
    * elsewhere that the enrichers reach non-public members that way, and using
    * the same trick here would record the reflection as much as the method. A
    * subclass is what a container adapter extending this enricher would have,
    * and that is the surface being read.
    */
   private static final class ResourceProbe extends ResourceInjectionEnricher
   {
      Object call(AnnotatedElement element) throws Exception
      {
         return resolveResource(element);
      }
   }

   private boolean resolveElement(Rec r, final AnnotatedElement element)
   {
      r.kv("element", element == null ? "<null>" : element.getClass().getSimpleName());
      r.value("resolveResource", () -> new ResourceProbe().call(element));
      return true;
   }

   // ------------------------------------------------------------------ @EJB

   /**
    * Fields and setters carrying {@code @EJB}.
    *
    * The observable here is a sequence, not a value: with no mapped name the
    * enricher tries eleven JNDI names in a fixed order and takes the first that
    * answers, so the log is the contract and the field is only the summary.
    */
   private boolean ejb(Rec r) throws Exception
   {
      if ("ejb-none-found".equals(name))
      {
         // Nothing bound: all eleven are tried and named back in the failure.
         return enrich(r, ejb(), new Beans.EjbPlain());
      }
      if ("ejb-first-name".equals(name))
      {
         Jndi.bind("java:global/test.ear/test/CartBean", new Ejbs.Bean("first"));
         return enrich(r, ejb(), new Beans.EjbPlain());
      }
      if ("ejb-middle-name".equals(name))
      {
         Jndi.bind("java:global/test/Cart/no-interface", new Ejbs.Bean("middle"));
         return enrich(r, ejb(), new Beans.EjbPlain());
      }
      if ("ejb-last-name".equals(name))
      {
         Jndi.bind("Cart/no-interface", new Ejbs.Bean("last"));
         return enrich(r, ejb(), new Beans.EjbPlain());
      }
      if ("ejb-two-names-bound".equals(name))
      {
         // Both answer; only the earlier one is ever asked.
         Jndi.bind("java:global/test/Cart", new Ejbs.Bean("third"));
         Jndi.bind("Cart/no-interface", new Ejbs.Bean("last"));
         return enrich(r, ejb(), new Beans.EjbPlain());
      }
      if ("ejb-other-type".equals(name))
      {
         // Every name is derived from the field's simple type name.
         Jndi.bind("Order/no-interface", new Ejbs.Bean("order"));
         return enrich(r, ejb(), new Beans.EjbOtherType());
      }
      if ("ejb-mapped".equals(name))
      {
         Jndi.bind("mapped/cart", new Ejbs.Bean("mapped"));
         return enrich(r, ejb(), new Beans.EjbMapped());
      }
      if ("ejb-mapped-not-found".equals(name))
      {
         // The list collapsed to one entry, so the failure names one entry.
         return enrich(r, ejb(), new Beans.EjbMapped());
      }
      if ("ejb-mapped-empty".equals(name))
      {
         // An empty mappedName is not a mapped name: the guard is
         // !mappedName.equals(""), so the full list is walked as if the
         // attribute were absent. Stopped at the third name to show the walk
         // rather than only its tail.
         Jndi.bind("java:global/test/Cart", new Ejbs.Bean("third"));
         return enrich(r, ejb(), new Beans.EjbMappedEmpty());
      }
      if ("ejb-preset".equals(name))
      {
         Jndi.bind("java:global/test.ear/test/CartBean", new Ejbs.Bean("first"));
         return enrich(r, ejb(), new Beans.EjbPreset());
      }
      if ("ejb-inherited".equals(name))
      {
         Jndi.bind("Cart/no-interface", new Ejbs.Bean("inherited"));
         return enrich(r, ejb(), new Beans.EjbSub());
      }
      if ("ejb-inherited-method".equals(name))
      {
         // The field walk climbs the hierarchy; the method walk does not. So
         // this injects nothing and looks nothing up, which is the whole point.
         Jndi.bind("Cart/no-interface", new Ejbs.Bean("inherited"));
         return enrich(r, ejb(), new Beans.InheritsMethod());
      }
      if ("ejb-lookup-throws-runtime".equals(name))
      {
         // The per-name catch is for NamingException only. Anything else escapes
         // the loop on the first name and is never retried.
         Jndi.failWith("java:global/test.ear/test/CartBean",
               new IllegalStateException("the directory said no"));
         Jndi.bind("Cart/no-interface", new Ejbs.Bean("never-reached"));
         return enrich(r, ejb(), new Beans.EjbPlain());
      }
      if ("ejb-setter".equals(name))
      {
         Jndi.bind("Cart/no-interface", new Ejbs.Bean("setter"));
         return enrich(r, ejb(), new Beans.EjbSetter());
      }
      if ("ejb-setter-tagged-parameter".equals(name))
      {
         Jndi.bind("CartBean/local", new Ejbs.Bean("ninth"));
         return enrich(r, ejb(), new Beans.EjbSetterTaggedParameter());
      }
      if ("ejb-setter-two-args".equals(name))
      {
         return enrich(r, ejb(), new Beans.EjbSetterTwoArgs());
      }
      if ("ejb-not-a-setter".equals(name))
      {
         return enrich(r, ejb(), new Beans.EjbNotASetter());
      }
      if ("ejb-setter-throws".equals(name))
      {
         Jndi.bind("mapped/cart", new Ejbs.Bean("setter"));
         return enrich(r, ejb(), new Beans.EjbSetterThrows());
      }
      if ("ejb-resolve".equals(name))
      {
         return resolve(r, ejb(), "generic");
      }
      return false;
   }

   // ------------------------------------------------------------------- CDI

   /**
    * The CDI enricher, both halves.
    *
    * {@code enrich} finds a bean manager in JNDI and hands it the test instance;
    * {@code resolve} finds one and asks it for a reference per parameter. Both
    * halves look the manager up the same way, through three names in order, and
    * both do nothing at all when none of them answers.
    */
   private boolean cdi(Rec r) throws Exception
   {
      if ("cdi-enrich-no-bm".equals(name))
      {
         // All three names miss. The enricher logs and returns; it does not fail.
         return enrich(r, cdi(), new Beans.CdiTarget());
      }
      if ("cdi-enrich-bm-standard".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().beanManager());
         return enrich(r, cdi(), new Beans.CdiTarget());
      }
      if ("cdi-enrich-bm-servlet".equals(name))
      {
         Jndi.bind(BM_SERVLET, manager().beanManager());
         return enrich(r, cdi(), new Beans.CdiTarget());
      }
      if ("cdi-enrich-bm-jbossas".equals(name))
      {
         Jndi.bind(BM_JBOSSAS, manager().beanManager());
         return enrich(r, cdi(), new Beans.CdiTarget());
      }
      if ("cdi-enrich-bm-wrong-type".equals(name))
      {
         // The cast, not the lookup, is what fails at the first name — and the
         // catch is on Exception, so a wrongly typed binding falls through to
         // the next name exactly like a missing one.
         Jndi.bind(BM_STANDARD, "not-a-bean-manager");
         Jndi.bind(BM_SERVLET, manager().beanManager());
         return enrich(r, cdi(), new Beans.CdiTarget());
      }
      if ("cdi-enrich-bm-lookup-throws".equals(name))
      {
         Jndi.failWith(BM_STANDARD, new IllegalStateException("the directory said no"));
         Jndi.bind(BM_JBOSSAS, manager().beanManager());
         return enrich(r, cdi(), new Beans.CdiTarget());
      }
      if ("cdi-enrich-no-field".equals(name))
      {
         // The injection target still runs; there is simply nothing to write.
         Jndi.bind(BM_STANDARD, manager().beanManager());
         return enrich(r, cdi(), new Beans.CdiNoField());
      }
      if ("cdi-enrich-creational-throws".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().throwFrom("createCreationalContext").beanManager());
         return enrich(r, cdi(), new Beans.CdiTarget());
      }
      if ("cdi-enrich-annotated-type-throws".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().throwFrom("createAnnotatedType").beanManager());
         return enrich(r, cdi(), new Beans.CdiTarget());
      }
      if ("cdi-enrich-injection-target-throws".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().throwFrom("createInjectionTarget").beanManager());
         return enrich(r, cdi(), new Beans.CdiTarget());
      }
      if ("cdi-enrich-inject-throws".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().throwFrom("inject").beanManager());
         return enrich(r, cdi(), new Beans.CdiTarget());
      }

      // ---- resolve: one injection point per parameter ----

      if ("cdi-resolve-no-bm".equals(name))
      {
         return resolve(r, cdi(), "two");
      }
      if ("cdi-resolve-zero-params".equals(name))
      {
         // The manager is still looked up before the loop that never runs.
         Jndi.bind(BM_STANDARD, manager().beanManager());
         return resolve(r, cdi(), "none");
      }
      if ("cdi-resolve-one-param".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().beanManager());
         return resolve(r, cdi(), "one");
      }
      if ("cdi-resolve-two-params".equals(name))
      {
         // A creational context per parameter, not one per call.
         Jndi.bind(BM_STANDARD, manager().beanManager());
         return resolve(r, cdi(), "two");
      }
      if ("cdi-resolve-generic-param".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().beanManager());
         return resolve(r, cdi(), "generic");
      }
      if ("cdi-resolve-primitive-param".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().beanManager());
         return resolve(r, cdi(), "primitive");
      }
      if ("cdi-resolve-array-param".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().beanManager());
         return resolve(r, cdi(), "array");
      }
      if ("cdi-resolve-unqualified-param".equals(name))
      {
         // The manager says nothing is a qualifier, so the injection point
         // invents @Default rather than reporting none.
         Jndi.bind(BM_STANDARD, manager().beanManager());
         return resolve(r, cdi(), "tagged");
      }
      if ("cdi-resolve-qualified-param".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().qualifiersAreQualifiers().beanManager());
         return resolve(r, cdi(), "tagged");
      }
      if ("cdi-resolve-two-qualifiers".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().qualifiersAreQualifiers().beanManager());
         return resolve(r, cdi(), "twoTagged");
      }
      if ("cdi-resolve-reference-null".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().reference(null).beanManager());
         return resolve(r, cdi(), "one");
      }
      if ("cdi-resolve-reference-throws".equals(name))
      {
         Jndi.bind(BM_STANDARD, manager().throwFrom("getInjectableReference").beanManager());
         return resolve(r, cdi(), "one");
      }
      return false;
   }

   // ----------------------------------------------------------- beans.xml

   /**
    * The protocol archive processor that copies a {@code beans.xml} marker.
    *
    * Pure archive surgery: given the user's deployment and the archive the
    * protocol is about to add, decide whether the protocol archive needs an
    * empty {@code beans.xml} so that the container will treat it as a bean
    * archive. Recorded as the protocol archive's contents before and after.
    */
   private boolean beansxml(Rec r)
   {
      if ("beansxml-protocol-is-application".equals(name))
      {
         // The protocol was merged into the user's own archive, so the user is
         // in control and the processor returns before looking at anything.
         JavaArchive app = ShrinkWrap.create(JavaArchive.class, "app.jar");
         app.addAsManifestResource(EmptyAsset.INSTANCE, "beans.xml");
         return process(r, app, app);
      }
      if ("beansxml-war-protocol".equals(name))
      {
         JavaArchive app = ShrinkWrap.create(JavaArchive.class, "app.jar");
         app.addAsManifestResource(EmptyAsset.INSTANCE, "beans.xml");
         return process(r, app, ShrinkWrap.create(WebArchive.class, "protocol.war"));
      }
      if ("beansxml-jar-protocol".equals(name))
      {
         WebArchive app = ShrinkWrap.create(WebArchive.class, "app.war");
         app.addAsWebInfResource(EmptyAsset.INSTANCE, "beans.xml");
         return process(r, app, ShrinkWrap.create(JavaArchive.class, "protocol.jar"));
      }
      if ("beansxml-other-protocol-type".equals(name))
      {
         // Neither a WebArchive nor a JavaArchive: the marker is found and then
         // there is nowhere to put it, and nothing says so.
         JavaArchive app = ShrinkWrap.create(JavaArchive.class, "app.jar");
         app.addAsManifestResource(EmptyAsset.INSTANCE, "beans.xml");
         return process(r, app, ShrinkWrap.create(EnterpriseArchive.class, "protocol.ear"));
      }
      if ("beansxml-none".equals(name))
      {
         return process(r, ShrinkWrap.create(JavaArchive.class, "app.jar"),
               ShrinkWrap.create(WebArchive.class, "protocol.war"));
      }
      if ("beansxml-at-root".equals(name))
      {
         // The filter is ".*/beans\.xml" and a root path still starts with a
         // slash, so a marker nobody would write by hand matches anyway.
         JavaArchive app = ShrinkWrap.create(JavaArchive.class, "app.jar");
         app.add(EmptyAsset.INSTANCE, "beans.xml");
         return process(r, app, ShrinkWrap.create(WebArchive.class, "protocol.war"));
      }
      if ("beansxml-wrong-name".equals(name))
      {
         JavaArchive app = ShrinkWrap.create(JavaArchive.class, "app.jar");
         app.addAsManifestResource(EmptyAsset.INSTANCE, "beans.xml.bak");
         return process(r, app, ShrinkWrap.create(WebArchive.class, "protocol.war"));
      }
      if ("beansxml-nested-jar".equals(name))
      {
         JavaArchive nested = ShrinkWrap.create(JavaArchive.class, "nested.jar");
         nested.addAsManifestResource(EmptyAsset.INSTANCE, "beans.xml");
         EnterpriseArchive app = ShrinkWrap.create(EnterpriseArchive.class, "app.ear");
         app.addAsModule(nested);
         return process(r, app, ShrinkWrap.create(WebArchive.class, "protocol.war"));
      }
      if ("beansxml-nested-war".equals(name))
      {
         WebArchive nested = ShrinkWrap.create(WebArchive.class, "nested.war");
         nested.addAsWebInfResource(EmptyAsset.INSTANCE, "beans.xml");
         EnterpriseArchive app = ShrinkWrap.create(EnterpriseArchive.class, "app.ear");
         app.addAsModule(nested);
         return process(r, app, ShrinkWrap.create(JavaArchive.class, "protocol.jar"));
      }
      if ("beansxml-nested-none".equals(name))
      {
         JavaArchive nested = ShrinkWrap.create(JavaArchive.class, "nested.jar");
         nested.addAsManifestResource(EmptyAsset.INSTANCE, "not-beans.xml");
         EnterpriseArchive app = ShrinkWrap.create(EnterpriseArchive.class, "app.ear");
         app.addAsModule(nested);
         return process(r, app, ShrinkWrap.create(WebArchive.class, "protocol.war"));
      }
      if ("beansxml-nested-deep".equals(name))
      {
         // The nested scan recurses without a depth limit: containsBeansXML
         // calls itself on every nested archive it finds, so a marker two
         // archives down is found just as a marker one down is. Recorded
         // because nothing in the method says how deep it intends to go, and a
         // rewrite that replaced the recursion with a single level would pass
         // every other fixture in this group.
         JavaArchive inner = ShrinkWrap.create(JavaArchive.class, "inner.jar");
         inner.addAsManifestResource(EmptyAsset.INSTANCE, "beans.xml");
         JavaArchive middle = ShrinkWrap.create(JavaArchive.class, "middle.jar");
         middle.add(inner, ArchivePaths.root(), ZipExporter.class);
         EnterpriseArchive app = ShrinkWrap.create(EnterpriseArchive.class, "app.ear");
         app.addAsModule(middle);
         return process(r, app, ShrinkWrap.create(WebArchive.class, "protocol.war"));
      }
      if ("beansxml-nested-not-an-archive".equals(name))
      {
         // A path that ends in .jar whose content is bytes rather than an
         // archive. The filter matches it and the instanceof rejects it.
         EnterpriseArchive app = ShrinkWrap.create(EnterpriseArchive.class, "app.ear");
         app.add(new ByteArrayAsset(new byte[] {1, 2, 3}), "opaque.jar");
         return process(r, app, ShrinkWrap.create(WebArchive.class, "protocol.war"));
      }
      if ("beansxml-nested-library".equals(name))
      {
         // Not at the root: the nested filter is "/.*\.(jar|war)" and a regex
         // dot matches a slash, so a library deep in WEB-INF is scanned too.
         JavaArchive nested = ShrinkWrap.create(JavaArchive.class, "nested.jar");
         nested.addAsManifestResource(EmptyAsset.INSTANCE, "beans.xml");
         WebArchive app = ShrinkWrap.create(WebArchive.class, "app.war");
         app.addAsLibrary(nested);
         return process(r, app, ShrinkWrap.create(JavaArchive.class, "protocol.jar"));
      }
      if ("beansxml-null-protocol".equals(name))
      {
         // The operand of a two-arm type dispatch, handed null.
         //
         // The marker is present, so the guard above the dispatch lets null
         // through to it; the application archive is not the protocol archive,
         // so the early return does not fire either. Both questions then
         // answer no and the method returns having done nothing at all. That
         // is the answer being recorded -- a quiet return, no throwable, an
         // archive with nothing added to it -- and it is an answer some ways
         // of writing the same dispatch do not give.
         JavaArchive app = ShrinkWrap.create(JavaArchive.class, "app.jar");
         app.addAsManifestResource(EmptyAsset.INSTANCE, "beans.xml");
         return processNullProtocol(r, app);
      }
      return false;
   }

   // --------------------------------------------------------------- osgi
   //
   // One method, reached directly because nothing else in this harness reaches
   // it. It is public and static, which is the whole of the surface: the rest
   // of this enricher wants an OSGi framework and a registered MBean, and this
   // method wants neither.

   private boolean osgi(Rec r)
   {
      if (!"osgi-server-first-of-factory".equals(name))
      {
         return false;
      }
      // Two servers, so the multi-server branch is taken and the one the
      // method settles on is a CHOICE rather than the only candidate.
      //
      // Recorded RELATIVE to a snapshot, never as an absolute count. The
      // factory is JVM-global and this suite deliberately leaks one server
      // into it elsewhere (jmx.executor-creates-server), so an absolute count
      // here would be a transcript of which fixtures ran first rather than a
      // fact about this method. What is recorded instead is only what holds
      // whatever the factory already contained: how far it grew under this
      // fixture's own hand, that the method returned the factory's FIRST
      // server rather than the newest or this fixture's own, and that it
      // created nothing while doing so.
      final List<MBeanServer> before = arqprobe.fam.jmx.Runners.snapshot();
      MBeanServerFactory.createMBeanServer();
      MBeanServerFactory.createMBeanServer();
      try
      {
         final List<MBeanServer> all = arqprobe.fam.jmx.Runners.snapshot();
         r.kv("factory.grew.by", Integer.valueOf(all.size() - before.size()));
         r.value("found.is.factory.first", () ->
               Boolean.valueOf(OSGiTestEnricher.findOrCreateMBeanServer() == all.get(0)));
         r.kv("created.by.call",
               Integer.valueOf(arqprobe.fam.jmx.Runners.createdSince(all)));
         // Called twice, because "find or create" naming two outcomes for one
         // return value is the kind of method a rewrite makes create on every
         // call without changing a signature.
         r.value("second.call.is.same", () ->
               Boolean.valueOf(OSGiTestEnricher.findOrCreateMBeanServer()
                     == OSGiTestEnricher.findOrCreateMBeanServer()));
         r.kv("created.by.three.calls",
               Integer.valueOf(arqprobe.fam.jmx.Runners.createdSince(all)));
      }
      finally
      {
         arqprobe.fam.jmx.Runners.releaseNew(before);
      }
      return true;
   }

   // --------------------------------------------------------------- drivers

   private boolean enrich(Rec r, final TestEnricher enricher, final Object testCase)
   {
      // The class is recorded as well as the outcome. Several pairs of fixtures
      // differ only in what was handed to the enricher and are supposed to come
      // out the same — an empty mappedName behaving exactly like no mappedName,
      // a parameter annotation the enricher cannot act on — and a transcript
      // that did not name the input could not tell that finding apart from two
      // ids that ran the same thing twice.
      r.kv("case", testCase.getClass().getSimpleName());
      r.attempt("enrich", () -> enricher.enrich(testCase));
      r.kv("instance", String.valueOf(testCase));
      return true;
   }

   private boolean resolve(Rec r, final TestEnricher enricher, String methodName)
   {
      final Method m = method(methodName);
      r.kv("method", m.getName());
      r.value("resolve", () -> render(enricher.resolve(m)));
      return true;
   }

   private boolean process(Rec r, Archive<?> application, Archive<?> protocol)
   {
      final TestDeployment deployment =
            new TestDeployment(application, new ArrayList<Archive<?>>());
      r.sorted("application.before", paths(application));
      r.sorted("protocol.before", paths(protocol));
      r.attempt("process", () -> new BeansXMLProtocolProcessor().process(deployment, protocol));
      r.sorted("application.after", paths(application));
      r.sorted("protocol.after", paths(protocol));
      return true;
   }

   /**
    * The same driver with no protocol archive, which {@link #paths} cannot be
    * asked about.
    *
    * A separate method rather than a null check inside {@link #process}: the
    * thirteen fixtures that use that one record five keys each and would start
    * recording a sixth state none of them is in.
    */
   private boolean processNullProtocol(Rec r, Archive<?> application)
   {
      final TestDeployment deployment =
            new TestDeployment(application, new ArrayList<Archive<?>>());
      r.sorted("application.before", paths(application));
      r.kv("protocol", null);
      r.attempt("process", () -> new BeansXMLProtocolProcessor().process(deployment, null));
      r.sorted("application.after", paths(application));
      return true;
   }

   // --------------------------------------------------------------- helpers

   private static TestEnricher resource()
   {
      return new ResourceInjectionEnricher();
   }

   private static TestEnricher ejb()
   {
      return new EJBInjectionEnricher();
   }

   private static TestEnricher cdi()
   {
      return new CDIInjectionEnricher();
   }

   private static Cdi.Manager manager()
   {
      return new Cdi.Manager();
   }

   /** Bind the name the enricher builds for a field with no {@code @Resource} attributes. */
   private static void bindField(Class<?> declaring, String field, Object value)
   {
      Jndi.bind(RES + "/" + declaring.getName() + "/" + field, value);
   }

   /**
    * One method of {@link Beans.Methods} by name.
    *
    * By name and not by signature because {@code getDeclaredMethods} returns
    * them in an order the JVM does not specify; every method there has a unique
    * name so exactly one matches whatever order they arrive in.
    */
   private static Method method(String methodName)
   {
      for (Method m : Beans.Methods.class.getDeclaredMethods())
      {
         if (m.getName().equals(methodName))
         {
            return m;
         }
      }
      throw new IllegalArgumentException("no such method: " + methodName);
   }

   private static String render(Object[] values)
   {
      if (values == null)
      {
         return "<null>";
      }
      StringBuilder sb = new StringBuilder();
      sb.append('[');
      for (int i = 0; i < values.length; i++)
      {
         if (i > 0)
         {
            sb.append(", ");
         }
         sb.append(values[i] == null ? "<null>" : Jndi.describe(values[i]));
      }
      return sb.append(']').toString();
   }

   private static List<String> paths(Archive<?> archive)
   {
      List<String> out = new ArrayList<String>();
      for (ArchivePath p : archive.getContent().keySet())
      {
         out.add(p.get());
      }
      return out;
   }
}
