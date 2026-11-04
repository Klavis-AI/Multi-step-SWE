package arqprobe.fam;

import java.io.File;
import java.io.FilenameFilter;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;

import arqprobe.Fixture;
import arqprobe.Rec;

import org.jboss.arquillian.packager.osgi.OSGiDeploymentPackager;
import org.jboss.arquillian.spi.TestDeployment;
import org.jboss.arquillian.spi.client.deployment.ProtocolArchiveProcessor;
import org.jboss.shrinkwrap.api.Archive;
import org.jboss.shrinkwrap.api.ShrinkWrap;
import org.jboss.shrinkwrap.api.asset.StringAsset;
import org.jboss.shrinkwrap.api.spec.EnterpriseArchive;
import org.jboss.shrinkwrap.api.spec.JavaArchive;
import org.jboss.shrinkwrap.api.spec.WebArchive;

/**
 * The OSGi deployment packager: is this archive a bundle, and if not, why not.
 *
 * {@code OSGiDeploymentPackager} is forty lines that do one thing — decide
 * whether what the test class built can be deployed to an OSGi container — and
 * it decides by building the jar for real. The archive is exported to a
 * temporary zip, handed to jboss-osgi's VFS, and read back as a bundle; the
 * answer is either the caller's own archive, unchanged, or an exception whose
 * message says what the manifest got wrong. Both halves matter, and the second
 * one is most of what is recorded here: eight distinct rejections, each with a
 * different sentence.
 *
 * <p>Three of those sentences do not belong to this codebase or to
 * jboss-osgi-spi, and the fixtures that raise them say so. A base name of one
 * character makes {@code File.createTempFile} reject the prefix, and the JVM's
 * wording for that changed at JDK 9; the two malformed-number cases go through
 * {@code Integer.parseInt}. The first is recorded as a type chain, the other
 * two in full — {@code NumberFormatException}'s decimal-radix message is the
 * one shape of that sentence the JDK has not touched, and it is checked on
 * every rung of the ladder rather than assumed.
 *
 * <p>The packager is also the only place in the tree that deletes what it
 * created, so one fixture watches the temporary directory across a successful
 * call: a rewrite that moved the export out of its {@code try} would pass every
 * other fixture here and leak a jar per deployment.
 *
 * <p>Code-driven family — the fixture name selects the scenario.
 */
public class PackFixture implements Fixture
{
   /** Enough of a manifest to be a valid OSGi R4 bundle. */
   private static final String VALID =
         "Manifest-Version: 1.0\n"
       + "Bundle-ManifestVersion: 2\n"
       + "Bundle-SymbolicName: com.acme.bundle\n"
       + "Bundle-Version: 1.0.0\n\n";

   /** The two headers every generated manifest below starts from. */
   private static final String R4 =
         "Manifest-Version: 1.0\n"
       + "Bundle-ManifestVersion: 2\n";

   private final String name;

   public PackFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      if (!(manifestFixture(r) || shapeFixture(r) || contractFixture(r)))
      {
         throw new IllegalArgumentException("unknown pack fixture: " + name);
      }
   }

   // ------------------------------------------------------------------------
   // what the manifest has to say
   // ------------------------------------------------------------------------

   private boolean manifestFixture(Rec r) throws Throwable
   {
      if ("valid-bundle".equals(name))
      {
         pack(r, bundle(VALID));
         return true;
      }
      if ("no-manifest".equals(name))
      {
         // The temporary jar's name reaches the message, which is why the
         // recorder collapses /tmp paths: the number in it is a nanosecond
         // clock reading.
         pack(r, bundle(null));
         return true;
      }
      if ("plain-manifest".equals(name))
      {
         pack(r, bundle("Manifest-Version: 1.0\n\n"));
         return true;
      }
      if ("manifest-only".equals(name))
      {
         // No content but the manifest. Enough to export, and enough to pass.
         pack(r, ShrinkWrap.create(JavaArchive.class, "bundle.jar")
               .setManifest(new StringAsset(VALID)));
         return true;
      }
      if ("manifest-no-version-header".equals(name))
      {
         pack(r, bundle("Bundle-ManifestVersion: 2\nBundle-SymbolicName: com.acme\n\n"));
         return true;
      }
      if ("manifest-crlf".equals(name))
      {
         pack(r, bundle("Manifest-Version: 1.0\r\nBundle-ManifestVersion: 2\r\n"
               + "Bundle-SymbolicName: com.acme\r\n\r\n"));
         return true;
      }
      if ("manifest-long-header".equals(name))
      {
         // Past the 72-column line the manifest writer wraps at, so the value
         // survives a round trip through continuation lines or it does not.
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.acme\nExport-Package: "
               + "a.very.long.package.name.that.keeps.going.and.going.past.seventy.two.columns\n\n"));
         return true;
      }
      if ("manifest-lowercase-path".equals(name))
      {
         // Added at meta-inf/manifest.mf rather than META-INF/MANIFEST.MF.
         pack(r, ShrinkWrap.create(JavaArchive.class, "bundle.jar")
               .add(new StringAsset(R4 + "Bundle-SymbolicName: com.acme\n\n"),
                     "meta-inf/manifest.mf"));
         return true;
      }
      if ("legacy-no-manifestversion".equals(name))
      {
         // No Bundle-ManifestVersion and no Bundle-SymbolicName: an OSGi R3
         // bundle, which this validator still accepts.
         pack(r, bundle("Manifest-Version: 1.0\nBundle-Name: legacy\n\n"));
         return true;
      }
      if ("legacy-with-symbolicname".equals(name))
      {
         // A symbolic name without the manifest version that introduced it.
         pack(r, bundle("Manifest-Version: 1.0\nBundle-SymbolicName: com.acme\n\n"));
         return true;
      }
      if ("bmv-missing".equals(name))
      {
         pack(r, bundle("Manifest-Version: 1.0\nBundle-ManifestVersion: 2\n\n"));
         return true;
      }
      if ("bmv-zero".equals(name))
      {
         pack(r, bundle("Manifest-Version: 1.0\nBundle-ManifestVersion: 0\n"
               + "Bundle-SymbolicName: com.acme\n\n"));
         return true;
      }
      if ("bmv-three".equals(name))
      {
         pack(r, bundle("Manifest-Version: 1.0\nBundle-ManifestVersion: 3\n"
               + "Bundle-SymbolicName: com.acme\n\n"));
         return true;
      }
      if ("bmv-not-a-number".equals(name))
      {
         pack(r, bundle("Manifest-Version: 1.0\nBundle-ManifestVersion: x\n"
               + "Bundle-SymbolicName: com.acme\n\n"));
         return true;
      }
      if ("bmv-extra-space".equals(name))
      {
         // Two spaces after the colon. The manifest parser strips exactly one,
         // so the value is " 2" and the number parse fails on the space --
         // a whitespace bug that only a byte-level fixture ever sees.
         pack(r, bundle("Manifest-Version: 1.0\nBundle-ManifestVersion:  2\n"
               + "Bundle-SymbolicName: com.acme\n\n"));
         return true;
      }
      if ("symbolicname-empty".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: \n\n"));
         return true;
      }
      if ("symbolicname-with-space".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: com acme\n\n"));
         return true;
      }
      if ("symbolicname-directive".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.acme;singleton:=true\n\n"));
         return true;
      }
      if ("symbolicname-duplicated".equals(name))
      {
         // java.util.jar logs a warning to stderr about the duplicate and keeps
         // the last value. The warning is not an observable -- it is not in the
         // transcript -- but the tolerance is.
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.first\n"
               + "Bundle-SymbolicName: com.second\n\n"));
         return true;
      }
      if ("fragment-host".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.acme\nFragment-Host: com.other\n\n"));
         return true;
      }
      if ("version-single-digit".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.acme\nBundle-Version: 1\n\n"));
         return true;
      }
      if ("version-qualifier".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.acme\nBundle-Version: 1.2.3.qual\n\n"));
         return true;
      }
      if ("version-empty".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.acme\nBundle-Version: \n\n"));
         return true;
      }
      if ("version-five-parts".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.acme\nBundle-Version: 1.2.3.4.5\n\n"));
         return true;
      }
      if ("version-negative".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.acme\nBundle-Version: -1.0.0\n\n"));
         return true;
      }
      if ("version-not-numeric".equals(name))
      {
         pack(r, bundle(R4 + "Bundle-SymbolicName: com.acme\nBundle-Version: not.a.version\n\n"));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // what the archive is, and what it is called
   // ------------------------------------------------------------------------

   private boolean shapeFixture(Rec r) throws Throwable
   {
      if ("shape-web-archive".equals(name))
      {
         pack(r, ShrinkWrap.create(WebArchive.class, "bundle.war")
               .setManifest(new StringAsset(VALID)));
         return true;
      }
      if ("shape-enterprise-archive".equals(name))
      {
         pack(r, ShrinkWrap.create(EnterpriseArchive.class, "bundle.ear")
               .setManifest(new StringAsset(VALID)));
         return true;
      }
      if ("shape-empty-archive".equals(name))
      {
         // Nothing at all in it: the zip exporter refuses before the manifest
         // is ever looked for, and the message is ShrinkWrap's own issue number.
         pack(r, ShrinkWrap.create(JavaArchive.class, "bundle.jar"));
         return true;
      }
      if ("shape-classes-only".equals(name))
      {
         pack(r, ShrinkWrap.create(JavaArchive.class, "bundle.jar").addClass(PackFixture.class));
         return true;
      }
      if ("name-no-extension".equals(name))
      {
         // No dot, so the whole name becomes the temporary file's prefix.
         pack(r, named("bundlename", VALID));
         return true;
      }
      if ("name-leading-dot".equals(name))
      {
         // lastIndexOf(".") is 0, which the guard requires to be > 0, so the
         // dot is kept and the prefix starts with it.
         pack(r, named(".jar", VALID));
         return true;
      }
      if ("name-two-dots".equals(name))
      {
         pack(r, named("bundle.core.jar", VALID));
         return true;
      }
      // A non-ASCII archive name is not among these. It was: the name reaches
      // the temporary file, and under the C locale the scored runner uses,
      // JDK 8 wrote it and JDK 11 onwards refuses -- so the fixture measured
      // sun.jnu.encoding rather than the tree, and the JDK 11 failure path
      // leaks two random mount ids the recorder cannot collapse. Measured on
      // all seven rungs, then dropped; see NOTES-authoring.md section 6.6.
      if ("name-too-short".equals(name))
      {
         // Base name "b" gives the prefix "b-", and createTempFile wants three
         // characters. Type chain only: the JVM reworded this at JDK 9.
         packTypes(r, named("b.jar", VALID));
         return true;
      }
      if ("name-with-separator".equals(name))
      {
         packTypes(r, named("x/y.jar", VALID));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // the packager's contract with its caller
   // ------------------------------------------------------------------------

   private boolean contractFixture(Rec r) throws Throwable
   {
      if ("null-deployment".equals(name))
      {
         r.attempt("generateDeployment(null, [])", () ->
               new OSGiDeploymentPackager().generateDeployment(
                     null, Collections.<ProtocolArchiveProcessor>emptyList()));
         return true;
      }
      if ("null-application-archive".equals(name))
      {
         r.attempt("new TestDeployment(null, [])", () ->
               new TestDeployment(null, new ArrayList<Archive<?>>()));
         return true;
      }
      if ("null-processors".equals(name))
      {
         // The processors argument is declared and never read. Passing null
         // rather than a list proves it, and would stop proving it the moment
         // somebody wired the extension point up.
         pack(r, bundle(VALID), null);
         return true;
      }
      if ("processors-not-called".equals(name))
      {
         // A processor that throws on contact. If it is ever invoked the
         // transcript says so, loudly, instead of quietly passing.
         Exploding processor = new Exploding();
         pack(r, bundle(VALID), Collections.<ProtocolArchiveProcessor>singletonList(processor));
         r.kv("processor.calls", Integer.valueOf(processor.calls));
         return true;
      }
      if ("auxiliary-archives-ignored".equals(name))
      {
         // handleArchive takes the auxiliary archives and does nothing with
         // them. Two of them, one of which is not a bundle at all.
         List<Archive<?>> aux = new ArrayList<Archive<?>>();
         aux.add(ShrinkWrap.create(JavaArchive.class, "aux-one.jar")
               .add(new StringAsset("a"), "a.txt"));
         aux.add(ShrinkWrap.create(WebArchive.class, "aux-two.war"));
         Archive<?> archive = bundle(VALID);
         TestDeployment deployment = new TestDeployment(archive, aux);
         r.kv("aux.count", Integer.valueOf(deployment.getAuxiliaryArchives().size()));
         generate(r, deployment, archive, Collections.<ProtocolArchiveProcessor>emptyList(), true);
         return true;
      }
      if ("packager-reused".equals(name))
      {
         // The packager holds no state, so a rejection must not poison the
         // next call and a success must not be cached.
         OSGiDeploymentPackager packager = new OSGiDeploymentPackager();
         Archive<?> good = bundle(VALID);
         Archive<?> bad = bundle(null);
         call(r, packager, "first", good);
         call(r, packager, "second", bad);
         call(r, packager, "third", good);
         return true;
      }
      if ("temp-file-removed".equals(name))
      {
         // The export writes a jar named after the archive and the finally
         // block deletes it. A distinctive base name so the count cannot pick
         // up anybody else's leftovers, and the directory is read before and
         // after so "there were none either way" cannot pass for a clean-up.
         final String base = "arqprobeleftover";
         File tmp = new File(System.getProperty("java.io.tmpdir"));
         FilenameFilter mine = new FilenameFilter()
         {
            public boolean accept(File dir, String n)
            {
               return n.startsWith(base + "-") && n.endsWith(".jar");
            }
         };
         r.kv("before", Integer.valueOf(count(tmp, mine)));
         pack(r, named(base + ".jar", VALID));
         r.kv("after", Integer.valueOf(count(tmp, mine)));
         return true;
      }
      if ("temp-file-removed-on-failure".equals(name))
      {
         // Same question for the path that throws: the delete is in a finally,
         // so a rejected bundle must not leave its export behind either.
         final String base = "arqprobefailure";
         File tmp = new File(System.getProperty("java.io.tmpdir"));
         FilenameFilter mine = new FilenameFilter()
         {
            public boolean accept(File dir, String n)
            {
               return n.startsWith(base + "-") && n.endsWith(".jar");
            }
         };
         r.kv("before", Integer.valueOf(count(tmp, mine)));
         pack(r, named(base + ".jar", null));
         r.kv("after", Integer.valueOf(count(tmp, mine)));
         return true;
      }
      if ("implements-packager".equals(name))
      {
         // The registration surface: what the container looks the packager up
         // as, and the one method it is expected to have.
         Class<?> c = OSGiDeploymentPackager.class;
         List<String> ifaces = new ArrayList<String>();
         for (Class<?> i : c.getInterfaces())
         {
            ifaces.add(i.getName());
         }
         Collections.sort(ifaces);
         r.list("interfaces", ifaces);
         List<String> methods = new ArrayList<String>();
         for (java.lang.reflect.Method m : c.getDeclaredMethods())
         {
            // Not a declaration; see Util.compilerMade for the same line drawn
            // over class files. A lambda anywhere in this class body adds a
            // lambda$... method here and would move a recording that is about
            // the registration surface.
            if (m.isSynthetic())
            {
               continue;
            }
            methods.add(java.lang.reflect.Modifier.toString(m.getModifiers())
                  + " " + m.getReturnType().getName() + " " + m.getName());
         }
         Collections.sort(methods);
         r.list("methods", methods);
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // helpers
   // ------------------------------------------------------------------------

   /** A jar called {@code bundle.jar} with one asset, plus the given manifest. */
   private static JavaArchive bundle(String manifest)
   {
      return named("bundle.jar", manifest);
   }

   private static JavaArchive named(String archiveName, String manifest)
   {
      JavaArchive jar = ShrinkWrap.create(JavaArchive.class, archiveName)
            .add(new StringAsset("x"), "x.txt");
      if (manifest != null)
      {
         jar.setManifest(new StringAsset(manifest));
      }
      return jar;
   }

   private static void pack(Rec r, Archive<?> archive) throws Throwable
   {
      pack(r, archive, Collections.<ProtocolArchiveProcessor>emptyList());
   }

   private static void pack(Rec r, Archive<?> archive, Collection<ProtocolArchiveProcessor> ps)
   {
      generate(r, new TestDeployment(archive, new ArrayList<Archive<?>>()), archive, ps, true);
   }

   /** {@link #pack}, recording a failure as its type chain only. */
   private static void packTypes(Rec r, Archive<?> archive)
   {
      generate(r, new TestDeployment(archive, new ArrayList<Archive<?>>()), archive,
            Collections.<ProtocolArchiveProcessor>emptyList(), false);
   }

   /**
    * One call, recorded three ways: did it return, did it return the caller's
    * own archive, and what does that archive say it holds.
    *
    * Identity rather than equality: the packager's contract on the happy path
    * is that it hands back exactly what it was given, and an implementation
    * that rebuilt an equal archive would be a different thing entirely.
    */
   private static void generate(Rec r, TestDeployment deployment, Archive<?> archive,
         Collection<ProtocolArchiveProcessor> processors, boolean fullMessages)
   {
      Archive<?> out;
      try
      {
         out = new OSGiDeploymentPackager().generateDeployment(deployment, processors);
      }
      catch (Throwable t)
      {
         r.kv("generate", "threw");
         if (fullMessages)
         {
            r.thrown("generate.ex", t);
         }
         else
         {
            r.thrownTypes("generate.ex", t);
         }
         return;
      }
      r.kv("generate", "ok");
      r.kv("same", Boolean.valueOf(out == archive));
      r.kv("out", out.toString());
   }

   private static void call(Rec r, OSGiDeploymentPackager packager, String key, Archive<?> archive)
   {
      try
      {
         Archive<?> out = packager.generateDeployment(
               new TestDeployment(archive, new ArrayList<Archive<?>>()),
               Collections.<ProtocolArchiveProcessor>emptyList());
         r.kv(key, "ok");
         r.kv(key + ".same", Boolean.valueOf(out == archive));
      }
      catch (Throwable t)
      {
         r.kv(key, "threw");
         r.thrown(key + ".ex", t);
      }
   }

   private static int count(File dir, FilenameFilter filter)
   {
      String[] found = dir.list(filter);
      return found == null ? -1 : found.length;
   }

   /** A processor that cannot be called without saying so. */
   private static final class Exploding implements ProtocolArchiveProcessor
   {
      int calls;

      public void process(TestDeployment testDeployment, Archive<?> protocolArchive)
      {
         calls++;
         throw new IllegalStateException("the packager invoked a processor");
      }
   }
}
