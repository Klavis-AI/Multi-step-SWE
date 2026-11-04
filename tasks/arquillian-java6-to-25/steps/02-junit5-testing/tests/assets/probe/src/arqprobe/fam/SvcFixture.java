package arqprobe.fam;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.svc.Greeter;
import arqprobe.fam.svc.Greeters;
import arqprobe.fam.svc.Services;

import org.jboss.arquillian.impl.DynamicServiceLoader;
import org.jboss.arquillian.spi.TestEnricher;
import org.jboss.arquillian.spi.TestRunner;
import org.jboss.arquillian.spi.util.ServiceLoader;
import org.jboss.arquillian.spi.util.TestEnrichers;
import org.jboss.arquillian.spi.util.TestRunners;

/**
 * Service discovery: how the two loaders read {@code META-INF/services}, which
 * malformed registrations each one tolerates, and how they construct a provider
 * once they have found it.
 *
 * The two implementations do not agree — one trims the line it read and the
 * other throws the trimmed copy away — and both reach a non-public constructor
 * with {@code setAccessible}. Both are the sort of thing a migration disturbs,
 * so the disagreement is recorded rather than smoothed over.
 *
 * Code-driven family — the fixture name selects the scenario.
 */
public class SvcFixture implements Fixture
{
   private static final String ALPHA = Greeters.Alpha.class.getName();

   private final String name;

   public SvcFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      if (!(dynamicFixture(r) || spiLoaderFixture(r) || runnersFixture(r) || enrichersFixture(r)))
      {
         throw new IllegalArgumentException("unknown svc fixture: " + name);
      }
   }

   // ------------------------------------------------------------------------
   // DynamicServiceLoader
   // ------------------------------------------------------------------------

   private boolean dynamicFixture(Rec r) throws Throwable
   {
      if ("dsl-all-none".equals(name))
      {
         all(r, new Services().declareNothing());
         return true;
      }
      if ("dsl-all-one".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeters.Alpha.class));
         return true;
      }
      if ("dsl-all-two".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeters.Alpha.class, Greeters.Beta.class));
         return true;
      }
      if ("dsl-all-declared-twice-in-one-file".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeters.Alpha.class, Greeters.Alpha.class));
         return true;
      }
      if ("dsl-all-declared-in-two-roots".equals(name))
      {
         all(r, new Services()
               .declare(Greeter.class, Greeters.Alpha.class)
               .declare(Greeter.class, Greeters.Beta.class));
         return true;
      }
      if ("dsl-all-comments-and-blanks".equals(name))
      {
         all(r, new Services().declare(Greeter.class,
               "# a leading comment\n\n" + ALPHA + "\n   \n# trailing\n"));
         return true;
      }
      if ("dsl-all-inline-comment".equals(name))
      {
         all(r, new Services().declare(Greeter.class, ALPHA + " # why this one\n"));
         return true;
      }
      if ("dsl-all-comment-before-name".equals(name))
      {
         all(r, new Services().declare(Greeter.class, "#" + ALPHA + "\n"));
         return true;
      }
      if ("dsl-all-surrounding-whitespace".equals(name))
      {
         all(r, new Services().declare(Greeter.class, "   " + ALPHA + "   \n"));
         return true;
      }
      if ("dsl-all-tab-separated".equals(name))
      {
         all(r, new Services().declare(Greeter.class, "\t" + ALPHA + "\t\n"));
         return true;
      }
      if ("dsl-all-crlf".equals(name))
      {
         all(r, new Services().declare(Greeter.class, ALPHA + "\r\n"));
         return true;
      }
      if ("dsl-all-no-trailing-newline".equals(name))
      {
         all(r, new Services().declare(Greeter.class, ALPHA));
         return true;
      }
      if ("dsl-all-empty-file".equals(name))
      {
         all(r, new Services().declare(Greeter.class, ""));
         return true;
      }
      if ("dsl-all-bom".equals(name))
      {
         all(r, new Services().declare(Greeter.class, "﻿" + ALPHA + "\n"));
         return true;
      }
      if ("dsl-all-missing-class".equals(name))
      {
         all(r, new Services().declare(Greeter.class, "arqprobe.fam.svc.NotOnThisClasspath\n"));
         return true;
      }
      if ("dsl-all-not-implementing".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeters.NotAGreeter.class.getName() + "\n"));
         return true;
      }
      if ("dsl-all-private-ctor".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeters.PrivateCtor.class));
         return true;
      }
      if ("dsl-all-no-noarg-ctor".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeters.NoNoArgCtor.class));
         return true;
      }
      if ("dsl-all-throwing-ctor".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeters.ThrowingCtor.class));
         return true;
      }
      if ("dsl-all-package-private-class".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeters.HIDDEN + "\n"));
         return true;
      }
      if ("dsl-all-abstract-provider".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeters.AbstractGreeter.class));
         return true;
      }
      if ("dsl-all-interface-named".equals(name))
      {
         all(r, new Services().declare(Greeter.class, Greeter.class));
         return true;
      }
      if ("dsl-null-arguments".equals(name))
      {
         final DynamicServiceLoader loader = new DynamicServiceLoader();
         final ClassLoader cl = new Services().declareNothing().loader();
         r.attempt("all(null, Greeter)", () -> loader.all(null, Greeter.class));
         r.attempt("all(cl, null)", () -> loader.all(cl, null));
         // The cast picks the ClassLoader overload; without it the null is
         // equally good as a service class and javac refuses the call.
         r.attempt("onlyOne(null, Greeter)", () -> loader.onlyOne((ClassLoader) null, Greeter.class));
         r.attempt("onlyOne(cl, null)", () -> loader.onlyOne(cl, (Class<Greeter>) null));
         r.attempt("onlyOne(cl, Greeter, null)", () -> loader.onlyOne(cl, Greeter.class, null));
         return true;
      }
      if ("dsl-onlyone-none".equals(name))
      {
         onlyOne(r, new Services().declareNothing());
         return true;
      }
      if ("dsl-onlyone-one".equals(name))
      {
         onlyOne(r, new Services().declare(Greeter.class, Greeters.Alpha.class));
         return true;
      }
      if ("dsl-onlyone-two-different".equals(name))
      {
         onlyOne(r, new Services().declare(Greeter.class, Greeters.Alpha.class, Greeters.Beta.class));
         return true;
      }
      if ("dsl-onlyone-same-class-twice".equals(name))
      {
         // Two roots, so the set keeps one entry but the loader saw two files.
         onlyOne(r, new Services()
               .declare(Greeter.class, Greeters.Alpha.class)
               .declare(Greeter.class, Greeters.Alpha.class));
         return true;
      }
      if ("dsl-onlyone-three-different".equals(name))
      {
         onlyOne(r, new Services().declare(Greeter.class,
               Greeters.Alpha.class, Greeters.Beta.class, Greeters.PrivateCtor.class));
         return true;
      }
      if ("dsl-default-none-declared".equals(name))
      {
         withDefault(r, new Services().declareNothing());
         return true;
      }
      if ("dsl-default-overridden".equals(name))
      {
         withDefault(r, new Services().declare(Greeter.class, Greeters.Alpha.class));
         return true;
      }
      if ("dsl-default-two-different".equals(name))
      {
         withDefault(r, new Services().declare(Greeter.class, Greeters.Alpha.class, Greeters.Beta.class));
         return true;
      }
      if ("dsl-default-is-unbuildable".equals(name))
      {
         final DynamicServiceLoader loader = new DynamicServiceLoader();
         final ClassLoader cl = new Services().declareNothing().loader();
         r.value("onlyOne", () -> describe(loader.onlyOne(cl, Greeter.class, Greeters.NoNoArgCtor.class)));
         return true;
      }
      if ("dsl-tccl-all".equals(name))
      {
         final DynamicServiceLoader loader = new DynamicServiceLoader();
         new Services().declare(Greeter.class, Greeters.Alpha.class)
               .asContextClassLoader(() -> r.valueList("all", () -> describeAll(loader.all(Greeter.class))));
         return true;
      }
      if ("dsl-tccl-onlyone".equals(name))
      {
         final DynamicServiceLoader loader = new DynamicServiceLoader();
         new Services().declare(Greeter.class, Greeters.Beta.class)
               .asContextClassLoader(() -> r.value("onlyOne", () -> describe(loader.onlyOne(Greeter.class))));
         return true;
      }
      if ("dsl-tccl-default".equals(name))
      {
         final DynamicServiceLoader loader = new DynamicServiceLoader();
         new Services().declareNothing().asContextClassLoader(
               () -> r.value("onlyOne", () -> describe(
                     loader.onlyOne(Greeter.class, Greeters.Beta.class))));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // spi.util.ServiceLoader
   // ------------------------------------------------------------------------

   private boolean spiLoaderFixture(Rec r) throws Throwable
   {
      if ("sl-none".equals(name))
      {
         providers(r, new Services().declareNothing());
         return true;
      }
      if ("sl-one".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, Greeters.Alpha.class));
         return true;
      }
      if ("sl-two".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, Greeters.Alpha.class, Greeters.Beta.class));
         return true;
      }
      if ("sl-surrounding-whitespace".equals(name))
      {
         // This loader computes the trimmed line and then discards it.
         providers(r, new Services().declare(Greeter.class, "   " + ALPHA + "   \n"));
         return true;
      }
      if ("sl-inline-comment".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, ALPHA + " # why this one\n"));
         return true;
      }
      if ("sl-comment-and-blanks".equals(name))
      {
         providers(r, new Services().declare(Greeter.class,
               "# a leading comment\n\n" + ALPHA + "\n# trailing\n"));
         return true;
      }
      if ("sl-crlf".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, ALPHA + "\r\n"));
         return true;
      }
      if ("sl-bom".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, "﻿" + ALPHA + "\n"));
         return true;
      }
      if ("sl-missing-class".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, "arqprobe.fam.svc.NotOnThisClasspath\n"));
         return true;
      }
      if ("sl-not-implementing".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, Greeters.NotAGreeter.class.getName() + "\n"));
         return true;
      }
      if ("sl-private-ctor".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, Greeters.PrivateCtor.class));
         return true;
      }
      if ("sl-no-noarg-ctor".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, Greeters.NoNoArgCtor.class));
         return true;
      }
      if ("sl-throwing-ctor".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, Greeters.ThrowingCtor.class));
         return true;
      }
      if ("sl-abstract-provider".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, Greeters.AbstractGreeter.class));
         return true;
      }
      if ("sl-package-private-class".equals(name))
      {
         providers(r, new Services().declare(Greeter.class, Greeters.HIDDEN + "\n"));
         return true;
      }
      if ("sl-mixed-good-and-bad".equals(name))
      {
         providers(r, new Services().declare(Greeter.class,
               "arqprobe.fam.svc.NotOnThisClasspath\n" + ALPHA + "\n"));
         return true;
      }
      if ("sl-iterator-is-cached".equals(name))
      {
         ServiceLoader<Greeter> loader = ServiceLoader.load(Greeter.class,
               new Services().declare(Greeter.class, Greeters.Alpha.class).loader());
         Greeter first = loader.iterator().next();
         Greeter second = loader.iterator().next();
         r.kv("same.instance", Boolean.valueOf(first == second));
         loader.reload();
         Greeter third = loader.iterator().next();
         r.kv("same.after.reload", Boolean.valueOf(first == third));
         r.kv("greet", third.greet());
         return true;
      }
      if ("sl-get-providers-unmodifiable".equals(name))
      {
         final ServiceLoader<Greeter> loader = ServiceLoader.load(Greeter.class,
               new Services().declare(Greeter.class, Greeters.Alpha.class).loader());
         r.kv("size", Integer.valueOf(loader.getProviders().size()));
         r.attempt("add", () -> loader.getProviders().add(new Greeters.Beta()));
         r.attempt("clear", () -> loader.getProviders().clear());
         return true;
      }
      if ("sl-tostring".equals(name))
      {
         r.kv("toString", ServiceLoader.load(Greeter.class,
               new Services().declareNothing().loader()).toString());
         return true;
      }
      if ("sl-load-installed".equals(name))
      {
         r.attempt("loadInstalled", () -> ServiceLoader.loadInstalled(Greeter.class));
         return true;
      }
      if ("sl-null-loader-falls-back".equals(name))
      {
         ServiceLoader<Greeter> loader = ServiceLoader.load(Greeter.class, null);
         r.kv("toString", loader.toString());
         r.kv("providers.size", Integer.valueOf(loader.getProviders().size()));
         return true;
      }
      if ("sl-create-instance-direct".equals(name))
      {
         final ServiceLoader<Greeter> loader = ServiceLoader.load(Greeter.class,
               new Services().declareNothing().loader());
         r.value("alpha", () -> loader.createInstance(ALPHA).greet());
         r.value("private", () -> loader.createInstance(Greeters.PrivateCtor.class.getName()).greet());
         r.attempt("missing", () -> loader.createInstance("arqprobe.fam.svc.NotOnThisClasspath"));
         r.attempt("not-implementing", () -> loader.createInstance(Greeters.NotAGreeter.class.getName()));
         r.attempt("no-noarg", () -> loader.createInstance(Greeters.NoNoArgCtor.class.getName()));
         r.attempt("abstract", () -> loader.createInstance(Greeters.AbstractGreeter.class.getName()));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // TestRunners
   // ------------------------------------------------------------------------

   private boolean runnersFixture(Rec r) throws Throwable
   {
      if ("runners-none".equals(name))
      {
         final ClassLoader cl = new Services().declareNothing().loader();
         r.attempt("getTestRunner", () -> TestRunners.getTestRunner(cl));
         return true;
      }
      if ("runners-one".equals(name))
      {
         final ClassLoader cl = new Services()
               .declare(TestRunner.class, Greeters.RunnerOne.class).loader();
         r.value("type", () -> TestRunners.getTestRunner(cl).getClass().getName());
         r.value("execute", () -> TestRunners.getTestRunner(cl)
               .execute(Greeters.EnrichmentTarget.class, "noArguments").getStatus());
         return true;
      }
      if ("runners-two".equals(name))
      {
         final ClassLoader cl = new Services()
               .declare(TestRunner.class, Greeters.RunnerOne.class, Greeters.RunnerTwo.class).loader();
         r.attempt("getTestRunner", () -> TestRunners.getTestRunner(cl));
         return true;
      }
      if ("runners-tccl".equals(name))
      {
         new Services().declare(TestRunner.class, Greeters.RunnerTwo.class)
               .asContextClassLoader(() -> r.value("type",
                     () -> TestRunners.getTestRunner().getClass().getName()));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // TestEnrichers
   // ------------------------------------------------------------------------

   private boolean enrichersFixture(Rec r) throws Throwable
   {
      if ("enrichers-none".equals(name))
      {
         enrich(r, new Services().declareNothing(), "twoArguments");
         return true;
      }
      if ("enrichers-one".equals(name))
      {
         enrich(r, new Services().declare(TestEnricher.class, Greeters.FirstEnricher.class),
               "twoArguments");
         return true;
      }
      if ("enrichers-first-wins".equals(name))
      {
         enrich(r, new Services().declare(TestEnricher.class,
               Greeters.FirstEnricher.class, Greeters.SecondEnricher.class), "twoArguments");
         return true;
      }
      if ("enrichers-empty-then-filled".equals(name))
      {
         enrich(r, new Services().declare(TestEnricher.class,
               Greeters.EmptyEnricher.class, Greeters.SecondEnricher.class), "twoArguments");
         return true;
      }
      if ("enrichers-null-result-ignored".equals(name))
      {
         enrich(r, new Services().declare(TestEnricher.class,
               Greeters.NullEnricher.class, Greeters.FirstEnricher.class), "twoArguments");
         return true;
      }
      if ("enrichers-wrong-arity".equals(name))
      {
         enrich(r, new Services().declare(TestEnricher.class, Greeters.WrongArityEnricher.class),
               "twoArguments");
         return true;
      }
      if ("enrichers-no-parameters".equals(name))
      {
         enrich(r, new Services().declare(TestEnricher.class, Greeters.FirstEnricher.class),
               "noArguments");
         return true;
      }
      if ("enrichers-null-method".equals(name))
      {
         new Services().declare(TestEnricher.class, Greeters.FirstEnricher.class)
               .asContextClassLoader(() -> r.attempt("enrich(null)", () -> TestEnrichers.enrich(null)));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // helpers
   // ------------------------------------------------------------------------

   private static void all(Rec r, Services services) throws Exception
   {
      final DynamicServiceLoader loader = new DynamicServiceLoader();
      final ClassLoader cl = services.loader();
      r.valueList("all", () -> describeAll(loader.all(cl, Greeter.class)));
   }

   private static void onlyOne(Rec r, Services services) throws Exception
   {
      final DynamicServiceLoader loader = new DynamicServiceLoader();
      final ClassLoader cl = services.loader();
      r.value("onlyOne", () -> describe(loader.onlyOne(cl, Greeter.class)));
   }

   private static void withDefault(Rec r, Services services) throws Exception
   {
      final DynamicServiceLoader loader = new DynamicServiceLoader();
      final ClassLoader cl = services.loader();
      r.value("onlyOne.default", () -> describe(loader.onlyOne(cl, Greeter.class, Greeters.Beta.class)));
   }

   private static void providers(Rec r, Services services) throws Exception
   {
      final ServiceLoader<Greeter> loader = ServiceLoader.load(Greeter.class, services.loader());
      r.valueList("providers", () -> describeAll(loader.getProviders()));
   }

   private static void enrich(Rec r, Services services, final String method) throws Exception
   {
      services.asContextClassLoader(() -> r.value("resolved", () -> Arrays.deepToString(
            TestEnrichers.enrich(
                  Greeters.EnrichmentTarget.class.getMethod(method, methodTypes(method))))));
   }

   private static Class<?>[] methodTypes(String method)
   {
      return "twoArguments".equals(method)
            ? new Class<?>[] {String.class, Integer.class}
            : new Class<?>[0];
   }

   private static String describe(Object provider)
   {
      if (provider == null)
      {
         return "<null>";
      }
      return provider.getClass().getName()
            + (provider instanceof Greeter ? "/" + ((Greeter) provider).greet() : "");
   }

   private static List<String> describeAll(Collection<?> providers)
   {
      List<String> out = new ArrayList<String>();
      for (Object provider : providers)
      {
         out.add(describe(provider));
      }
      return out;
   }
}
