package arqprobe.fam;

import java.io.File;
import java.lang.reflect.Field;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.life.Containers;
import arqprobe.fam.tool.Samples;

import org.jboss.arquillian.impl.client.deployment.tool.ArchiveDeploymentToolingExporter;
import org.jboss.arquillian.impl.client.deployment.tool.ToolingDeploymentFormatter;
import org.jboss.arquillian.spi.client.deployment.DeploymentDescription;
import org.jboss.arquillian.spi.event.container.BeforeDeploy;
import org.jboss.shrinkwrap.api.Archive;
import org.jboss.shrinkwrap.api.ArchivePaths;
import org.jboss.shrinkwrap.api.Node;
import org.jboss.shrinkwrap.api.ShrinkWrap;
import org.jboss.shrinkwrap.api.asset.ByteArrayAsset;
import org.jboss.shrinkwrap.api.asset.EmptyAsset;
import org.jboss.shrinkwrap.api.asset.FileAsset;
import org.jboss.shrinkwrap.api.asset.StringAsset;
import org.jboss.shrinkwrap.api.asset.UrlAsset;
import org.jboss.shrinkwrap.api.exporter.ZipExporter;
import org.jboss.shrinkwrap.api.spec.EnterpriseArchive;
import org.jboss.shrinkwrap.api.spec.JavaArchive;
import org.jboss.shrinkwrap.api.spec.WebArchive;
import org.jboss.shrinkwrap.impl.base.asset.ClassLoaderAsset;

/**
 * The tooling view of a deployment: the XML an IDE is handed, and the handler
 * that writes it out.
 *
 * {@code ToolingDeploymentFormatter} is a {@code Formatter} — a ShrinkWrap
 * extension point — and its whole observable is one string. That string is
 * assembled by hand, with no escaping anywhere: an archive whose name contains
 * a quote produces XML that is not well formed, and the fixtures that feed it
 * one are here to pin that rather than to complain about it. A migration that
 * "fixed" the escaping would change what tooling reads, which is exactly the
 * kind of unasked-for behaviour change this suite exists to catch.
 *
 * <p>The other half of the class is a reflective peek into ShrinkWrap's own
 * internals: for four asset types it reaches a private field to recover where
 * the content came from, by {@code getDeclaredField} plus {@code setAccessible}.
 * That is the single most JDK-sensitive line in the package — it is the shape
 * of call that strong encapsulation broke for JDK-internal targets — and the
 * asset cases below walk every branch of it, including the three that return
 * {@code null} because the asset kind is not one of the four.
 *
 * <p>{@code ArchiveDeploymentToolingExporter} is the observer that would write
 * the file. Most of its body is commented out upstream, so {@code export} is
 * three early returns; they are recorded as they are, because "this handler
 * writes nothing" is the behaviour a migration must preserve. Its
 * {@code writeOutToFile} is live and is reached through a subclass.
 *
 * <p>Code-driven family — the fixture name selects the scenario.
 */
public class ToolFixture implements Fixture
{
   /** The formatter is constructed with a test class; this is the ordinary one. */
   private static final Class<?> TEST_CLASS = Samples.Alpha.class;

   /**
    * A resource that is certainly on the class path, because it is this class.
    *
    * {@code ClassLoaderAsset} resolves its name in its constructor and rejects
    * one it cannot find, so the happy path needs a real resource; the harness's
    * own class file is the one thing guaranteed to be there on every rung.
    */
   private static final String RESOURCE = "arqprobe/fam/ToolFixture.class";

   private final String name;

   public ToolFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      if (!(formatFixture(r) || assetFixture(r) || textFixture(r) || exportFixture(r)))
      {
         throw new IllegalArgumentException("unknown tool fixture: " + name);
      }
   }

   // ------------------------------------------------------------------------
   // archive shapes
   // ------------------------------------------------------------------------

   private boolean formatFixture(Rec r) throws Throwable
   {
      if ("fmt-empty-jar".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "empty.jar"));
         return true;
      }
      if ("fmt-empty-war".equals(name))
      {
         format(r, ShrinkWrap.create(WebArchive.class, "empty.war"));
         return true;
      }
      if ("fmt-empty-ear".equals(name))
      {
         format(r, ShrinkWrap.create(EnterpriseArchive.class, "empty.ear"));
         return true;
      }
      if ("fmt-one-asset".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "one.jar")
               .add(new StringAsset("hello"), "a.txt"));
         return true;
      }
      if ("fmt-two-assets".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "two.jar")
               .add(new StringAsset("a"), "a.txt")
               .add(new StringAsset("b"), "b.txt"));
         return true;
      }
      if ("fmt-nested-paths".equals(name))
      {
         // Every path segment becomes a Directory node of its own, so four
         // segments produce four nodes above the asset.
         format(r, ShrinkWrap.create(JavaArchive.class, "deep.jar")
               .add(new StringAsset("x"), "a/b/c/d.txt"));
         return true;
      }
      if ("fmt-explicit-directory".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "dir.jar")
               .addDirectory("/only/a/directory"));
         return true;
      }
      if ("fmt-directory-and-sibling".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "mixed.jar")
               .addDirectory("/shared/sub")
               .add(new StringAsset("x"), "/shared/file.txt"));
         return true;
      }
      if ("fmt-many-assets".equals(name))
      {
         // Twenty siblings under one parent. The formatter walks
         // Node.getChildren(), which is a Set: the order this records is the
         // order tooling actually receives, and pinning it is the point.
         JavaArchive jar = ShrinkWrap.create(JavaArchive.class, "many.jar");
         for (int i = 0; i < 20; i++)
         {
            jar.add(new StringAsset("v" + i), "content/item" + i + ".txt");
         }
         format(r, jar);
         return true;
      }
      if ("fmt-manifest".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "mf.jar")
               .setManifest(new StringAsset("Manifest-Version: 1.0\n")));
         return true;
      }
      if ("fmt-service-provider".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "svc.jar")
               .addAsServiceProvider(CharSequence.class, String.class));
         return true;
      }
      if ("fmt-war-web-resource".equals(name))
      {
         format(r, ShrinkWrap.create(WebArchive.class, "web.war")
               .addAsWebResource(new StringAsset("<html/>"), "index.html")
               .setWebXML(new StringAsset("<web-app/>")));
         return true;
      }
      if ("fmt-ear-module".equals(name))
      {
         format(r, ShrinkWrap.create(EnterpriseArchive.class, "app.ear")
               .addAsModule(ShrinkWrap.create(JavaArchive.class, "beans.jar")
                     .add(new StringAsset("b"), "b.txt")));
         return true;
      }
      if ("fmt-nested-archive".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "outer.jar")
               .add(ShrinkWrap.create(JavaArchive.class, "inner.jar")
                     .add(new StringAsset("i"), "i.txt"), "/lib", ZipExporter.class));
         return true;
      }
      if ("fmt-nested-archive-empty".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "outer.jar")
               .add(ShrinkWrap.create(JavaArchive.class, "hollow.jar"), "/lib", ZipExporter.class));
         return true;
      }
      if ("fmt-nested-archive-two-deep".equals(name))
      {
         // An ArchiveAsset inside an ArchiveAsset: formatNode recurses through
         // both, and the closing </asset> tags have to nest correctly.
         JavaArchive innermost = ShrinkWrap.create(JavaArchive.class, "innermost.jar")
               .add(new StringAsset("x"), "x.txt");
         JavaArchive middle = ShrinkWrap.create(JavaArchive.class, "middle.jar")
               .add(innermost, "/lib", ZipExporter.class);
         format(r, ShrinkWrap.create(JavaArchive.class, "outer.jar")
               .add(middle, "/lib", ZipExporter.class));
         return true;
      }
      if ("fmt-nested-archive-siblings".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "outer.jar")
               .add(ShrinkWrap.create(JavaArchive.class, "one.jar"), "/lib/one.jar", ZipExporter.class)
               .add(ShrinkWrap.create(JavaArchive.class, "two.jar"), "/lib/two.jar", ZipExporter.class));
         return true;
      }
      if ("fmt-nested-archive-beside-asset".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "outer.jar")
               .add(ShrinkWrap.create(JavaArchive.class, "inner.jar"), "/lib", ZipExporter.class)
               .add(new StringAsset("plain"), "/lib/plain.txt"));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // asset kinds — the reflective source lookup
   // ------------------------------------------------------------------------

   private boolean assetFixture(Rec r) throws Throwable
   {
      if ("asset-string".equals(name))
      {
         // Not one of the four kinds the lookup knows, so no source attribute.
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(new StringAsset("text"), "s.txt"));
         return true;
      }
      if ("asset-byte-array".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(new ByteArrayAsset(new byte[] {1, 2, 3}), "b.bin"));
         return true;
      }
      if ("asset-empty".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(EmptyAsset.INSTANCE, "e.txt"));
         return true;
      }
      if ("asset-class".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar").addClass(Samples.Alpha.class));
         return true;
      }
      if ("asset-class-inner".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar").addClass(Samples.Inner.class));
         return true;
      }
      if ("asset-class-deeper".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar").addClass(Samples.Inner.Deeper.class));
         return true;
      }
      if ("asset-class-anonymous".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .addClass(Samples.ANONYMOUS.getClass()));
         return true;
      }
      if ("asset-classes-two".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .addClasses(Samples.Alpha.class, Samples.Inner.class));
         return true;
      }
      if ("asset-classloader".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(new ClassLoaderAsset(RESOURCE), "cl.txt"));
         return true;
      }
      if ("asset-classloader-explicit-loader".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(new ClassLoaderAsset(RESOURCE, ToolFixture.class.getClassLoader()), "cl.txt"));
         return true;
      }
      if ("asset-classloader-absent".equals(name))
      {
         // ClassLoaderAsset resolves at construction, so a missing resource is
         // rejected before the formatter ever sees it. Type chain only: the
         // message names the application class loader, and that class was
         // renamed from sun.misc.Launcher$AppClassLoader to
         // jdk.internal.loader.ClassLoaders$AppClassLoader at JDK 9 -- an
         // expectation about the JVM's own machinery, not about this tree.
         r.valueTypeOnly("absent", () -> new ClassLoaderAsset("arqprobe/fam/tool/absent.txt"));
         return true;
      }
      if ("asset-file".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(new FileAsset(Samples.file("payload.txt", "p")), "f.txt"));
         return true;
      }
      if ("asset-file-relative".equals(name))
      {
         // getAbsolutePath resolves against the working directory, which the
         // scored runner does not fix, so the fixture supplies an absolute
         // parent and a relative-looking name rather than a relative path.
         File f = Samples.file("nested.txt", "n");
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(new FileAsset(new File(f.getParentFile(), "./nested.txt")), "f.txt"));
         return true;
      }
      if ("asset-url-file".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(new UrlAsset(Samples.file("u.txt", "u").toURI().toURL()), "u.txt"));
         return true;
      }
      if ("asset-url-http".equals(name))
      {
         // Never opened; the formatter only asks the URL for its external form.
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(new UrlAsset(new URL("http://example.org/a.txt?q=1&r=2")), "u.txt"));
         return true;
      }
      if ("asset-url-jar".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a.jar")
               .add(new UrlAsset(new URL("jar:file:/tmp/x.jar!/inside.txt")), "u.txt"));
         return true;
      }
      if ("asset-every-kind".equals(name))
      {
         // All four recognised kinds and two unrecognised ones in one archive,
         // so the four source spellings sit side by side in one transcript.
         format(r, ShrinkWrap.create(JavaArchive.class, "all.jar")
               .add(new StringAsset("s"), "kinds/string.txt")
               .add(EmptyAsset.INSTANCE, "kinds/empty.txt")
               .add(new FileAsset(Samples.file("f.txt", "f")), "kinds/file.txt")
               .add(new UrlAsset(new URL("http://example.org/u")), "kinds/url.txt")
               .add(new ClassLoaderAsset(RESOURCE), "kinds/cl.txt")
               .addClass(Samples.Alpha.class));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // the text the formatter emits, and what it does not escape
   // ------------------------------------------------------------------------

   private boolean textFixture(Rec r) throws Throwable
   {
      if ("text-name-ampersand".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a&b.jar"));
         return true;
      }
      if ("text-name-quote".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a\"b.jar"));
         return true;
      }
      if ("text-name-angle".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "a<b>.jar"));
         return true;
      }
      if ("text-name-unicode".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "é中.jar"));
         return true;
      }
      if ("text-path-ampersand".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "p.jar")
               .add(new StringAsset("x"), "a&b.txt"));
         return true;
      }
      if ("text-path-quote".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "p.jar")
               .add(new StringAsset("x"), "a\"b.txt"));
         return true;
      }
      if ("text-path-angle".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "p.jar")
               .add(new StringAsset("x"), "a<b>.txt"));
         return true;
      }
      if ("text-path-unicode".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "p.jar")
               .add(new StringAsset("x"), "héllo/中.txt"));
         return true;
      }
      if ("text-testclass-inner".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "t.jar"), Samples.Inner.class);
         return true;
      }
      if ("text-testclass-anonymous".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "t.jar"), Samples.ANONYMOUS.getClass());
         return true;
      }
      if ("text-testclass-array".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "t.jar"), String[].class);
         return true;
      }
      if ("text-testclass-primitive".equals(name))
      {
         format(r, ShrinkWrap.create(JavaArchive.class, "t.jar"), Integer.TYPE);
         return true;
      }
      if ("text-ctor-null-testclass".equals(name))
      {
         r.attempt("new ToolingDeploymentFormatter(null)",
               () -> new ToolingDeploymentFormatter(null));
         return true;
      }
      if ("text-format-null-archive".equals(name))
      {
         final ToolingDeploymentFormatter f = new ToolingDeploymentFormatter(TEST_CLASS);
         r.attempt("format(null)", () -> f.format(null));
         return true;
      }
      if ("text-format-node-null".equals(name))
      {
         final ToolingDeploymentFormatter f = new ToolingDeploymentFormatter(TEST_CLASS);
         r.attempt("formatNode(null, sb)", () -> f.formatNode(null, new StringBuilder()));
         final Node root = ShrinkWrap.create(JavaArchive.class, "n.jar").get(ArchivePaths.root());
         r.attempt("formatNode(root, null)", () -> f.formatNode(root, null));
         return true;
      }
      if ("text-format-node-subtree".equals(name))
      {
         // formatNode is public and takes any node, so tooling can format a
         // subtree without the <deployment> wrapper. Recorded because a public
         // method is part of the surface whether or not the codebase calls it.
         Archive<?> jar = ShrinkWrap.create(JavaArchive.class, "n.jar")
               .add(new StringAsset("a"), "sub/a.txt")
               .add(new StringAsset("b"), "sub/b.txt")
               .add(new StringAsset("c"), "other.txt");
         StringBuilder sb = new StringBuilder();
         new ToolingDeploymentFormatter(TEST_CLASS).formatNode(jar.get("/sub"), sb);
         lines(r, "xml", sb.toString());
         return true;
      }
      if ("text-via-archive-tostring".equals(name))
      {
         // The supported entry point: Archive.toString(Formatter). Records that
         // the Formatter contract and a direct call agree.
         Archive<?> jar = ShrinkWrap.create(JavaArchive.class, "t.jar")
               .add(new StringAsset("x"), "x.txt");
         ToolingDeploymentFormatter f = new ToolingDeploymentFormatter(TEST_CLASS);
         String viaArchive = jar.toString(f);
         String direct = f.format(jar);
         r.kv("agree", Boolean.valueOf(viaArchive.equals(direct)));
         lines(r, "xml", viaArchive);
         return true;
      }
      if ("text-formatter-reused".equals(name))
      {
         // One formatter, two archives: it holds the test class and nothing
         // else, so the second call must not see anything from the first.
         ToolingDeploymentFormatter f = new ToolingDeploymentFormatter(TEST_CLASS);
         lines(r, "first", f.format(ShrinkWrap.create(JavaArchive.class, "first.jar")
               .add(new StringAsset("1"), "one.txt")));
         lines(r, "second", f.format(ShrinkWrap.create(JavaArchive.class, "second.jar")
               .add(new StringAsset("2"), "two.txt")));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // the exporter
   // ------------------------------------------------------------------------

   private boolean exportFixture(Rec r) throws Throwable
   {
      if ("export-property-name".equals(name))
      {
         // The property key is package-private, so it is recovered rather than
         // restated: a transcript that repeated my guess would agree with
         // itself no matter what the field said.
         Field f = ArchiveDeploymentToolingExporter.class
               .getDeclaredField("ARQUILLIAN_TOOLING_DEPLOYMENT_FOLDER");
         f.setAccessible(true);
         r.kv("property", f.get(null));
         r.kv("modifiers", java.lang.reflect.Modifier.toString(f.getModifiers()));
         return true;
      }
      if ("export-property-absent".equals(name))
      {
         exportEvent(r, null, true);
         return true;
      }
      if ("export-property-set-no-testable".equals(name))
      {
         exportEvent(r, "/tmp/arqprobe-tooling-out", false);
         return true;
      }
      if ("export-property-set-with-testable".equals(name))
      {
         exportEvent(r, "/tmp/arqprobe-tooling-out", true);
         return true;
      }
      if ("export-property-empty".equals(name))
      {
         // An empty string is not null, so the handler goes past the first
         // guard — a distinction a rewritten guard could easily lose.
         exportEvent(r, "", true);
         return true;
      }
      if ("export-event-null-container".equals(name))
      {
         final DeploymentDescription description = new DeploymentDescription(
               "d", ShrinkWrap.create(JavaArchive.class, "d.jar"));
         r.attempt("new BeforeDeploy(null, description)",
               () -> new BeforeDeploy(null, description));
         return true;
      }
      if ("export-event-null-deployment".equals(name))
      {
         r.attempt("new BeforeDeploy(container, null)",
               () -> new BeforeDeploy(new Containers.Stub(), null));
         return true;
      }
      if ("write-ok".equals(name))
      {
         File target = new File(Samples.scratch(), "out.xml");
         new Samples.Exporter().write(target, "<deployment/>\n");
         r.kv("exists", Boolean.valueOf(target.exists()));
         r.kv("length", Long.valueOf(target.length()));
         r.kv("content", read(target));
         return true;
      }
      if ("write-empty-content".equals(name))
      {
         File target = new File(Samples.scratch(), "out.xml");
         new Samples.Exporter().write(target, "");
         r.kv("exists", Boolean.valueOf(target.exists()));
         r.kv("length", Long.valueOf(target.length()));
         return true;
      }
      if ("write-unicode-content".equals(name))
      {
         // content.getBytes() with no charset: the encoding is the platform
         // default, which the runner pins to UTF-8 on every rung of the ladder.
         File target = new File(Samples.scratch(), "out.xml");
         new Samples.Exporter().write(target, "<x>héllo 中</x>");
         r.kv("length", Long.valueOf(target.length()));
         r.kv("content", read(target));
         return true;
      }
      if ("write-overwrites".equals(name))
      {
         File target = new File(Samples.scratch(), "out.xml");
         Samples.Exporter e = new Samples.Exporter();
         e.write(target, "first and longer");
         e.write(target, "second");
         r.kv("length", Long.valueOf(target.length()));
         r.kv("content", read(target));
         return true;
      }
      if ("write-null-target".equals(name))
      {
         r.attempt("write(null, content)", () -> new Samples.Exporter().write(null, "x"));
         return true;
      }
      if ("write-null-content".equals(name))
      {
         final File target = new File(Samples.scratch(), "out.xml");
         r.attempt("write(target, null)", () -> new Samples.Exporter().write(target, null));
         r.kv("exists", Boolean.valueOf(target.exists()));
         return true;
      }
      if ("write-target-is-directory".equals(name))
      {
         final File target = Samples.scratch();
         r.attempt("write(dir, content)", () -> new Samples.Exporter().write(target, "x"));
         return true;
      }
      if ("write-missing-parent".equals(name))
      {
         final File target = new File(new File(Samples.scratch(), "absent"), "out.xml");
         r.attempt("write(missing/out.xml, content)",
               () -> new Samples.Exporter().write(target, "x"));
         return true;
      }
      if ("write-formatted-deployment".equals(name))
      {
         // The two halves joined the way the commented-out body would have
         // joined them: format an archive, write the result, read it back.
         File target = new File(Samples.scratch(), Samples.Alpha.class.getName() + ".xml");
         String xml = new ToolingDeploymentFormatter(TEST_CLASS).format(
               ShrinkWrap.create(JavaArchive.class, "deployed.jar")
                     .add(new StringAsset("x"), "x.txt"));
         new Samples.Exporter().write(target, xml);
         r.kv("name", target.getName());
         lines(r, "written", read(target));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // helpers
   // ------------------------------------------------------------------------

   private static void format(Rec r, Archive<?> archive) throws Throwable
   {
      format(r, archive, TEST_CLASS);
   }

   /**
    * Format one archive and record the XML a line at a time.
    *
    * Line at a time rather than as one value: the output is a dozen lines of
    * generated markup, and a transcript that carried it as a single key would
    * report "this line differs" for a document whose fourth line moved.
    */
   private static void format(Rec r, Archive<?> archive, Class<?> testClass) throws Throwable
   {
      r.kv("archive.name", archive.getName());
      r.kv("archive.entries", Integer.valueOf(archive.getContent().size()));
      lines(r, "xml", new ToolingDeploymentFormatter(testClass).format(archive));
   }

   private static void lines(Rec r, String key, String text)
   {
      List<String> out = new ArrayList<String>();
      int from = 0;
      while (from <= text.length())
      {
         int nl = text.indexOf('\n', from);
         if (nl < 0)
         {
            if (from < text.length())
            {
               out.add(text.substring(from));
            }
            break;
         }
         out.add(text.substring(from, nl));
         from = nl + 1;
      }
      r.list(key, out);
   }

   /**
    * Drive the observer with the tooling property in a known state.
    *
    * The property is global to the JVM and every other fixture shares it, so it
    * is restored in a finally — a leak here would show up as an unrelated
    * fixture changing depending on what ran before it.
    */
   private static void exportEvent(Rec r, final String folder, boolean testable) throws Throwable
   {
      Field field = ArchiveDeploymentToolingExporter.class
            .getDeclaredField("ARQUILLIAN_TOOLING_DEPLOYMENT_FOLDER");
      field.setAccessible(true);
      String key = (String) field.get(null);
      String previous = System.getProperty(key);
      try
      {
         if (folder == null)
         {
            System.clearProperty(key);
         }
         else
         {
            System.setProperty(key, folder);
         }
         Archive<?> archive = ShrinkWrap.create(JavaArchive.class, "deployed.jar")
               .add(new StringAsset("x"), "x.txt");
         DeploymentDescription description = new DeploymentDescription("d", archive);
         if (testable)
         {
            description.setTestableArchive(archive);
         }
         r.kv("testableArchive", description.getTestableArchive() == null ? "<null>" : "present");
         final BeforeDeploy event = new BeforeDeploy(new Containers.Stub(), description);
         final ArchiveDeploymentToolingExporter exporter = new ArchiveDeploymentToolingExporter();
         r.attempt("export", () -> exporter.export(event));
         r.kv("folder.created", Boolean.valueOf(folder != null && folder.length() > 0
               && new File(folder).exists()));
      }
      finally
      {
         if (previous == null)
         {
            System.clearProperty(key);
         }
         else
         {
            System.setProperty(key, previous);
         }
      }
   }

   private static String read(File f) throws Exception
   {
      byte[] buf = new byte[(int) f.length()];
      java.io.InputStream in = new java.io.FileInputStream(f);
      try
      {
         int off = 0;
         while (off < buf.length)
         {
            int n = in.read(buf, off, buf.length - off);
            if (n < 0)
            {
               break;
            }
            off += n;
         }
      }
      finally
      {
         in.close();
      }
      return new String(buf, "UTF-8");
   }
}
