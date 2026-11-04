package arqprobe.fam;

import java.io.File;
import java.lang.reflect.Method;
import java.net.URI;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.servlet.Archives;
import arqprobe.fam.servlet.Http;
import arqprobe.fam.servlet.Log;
import arqprobe.fam.servlet.Stubs;

import org.jboss.arquillian.protocol.servlet.BaseServletProtocol;
import org.jboss.arquillian.protocol.servlet.ServletMethodExecutor;
import org.jboss.arquillian.protocol.servlet.ServletProtocolConfiguration;
import org.jboss.arquillian.protocol.servlet.ServletUtil;
import org.jboss.arquillian.spi.TestDeployment;
import org.jboss.arquillian.spi.TestResult;
import org.jboss.arquillian.spi.client.deployment.AuxiliaryArchiveAppender;
import org.jboss.arquillian.spi.client.deployment.DeploymentPackager;
import org.jboss.arquillian.spi.client.deployment.ProtocolArchiveProcessor;
import org.jboss.arquillian.spi.client.protocol.metadata.HTTPContext;
import org.jboss.arquillian.spi.client.protocol.metadata.ProtocolMetaData;
import org.jboss.arquillian.spi.client.protocol.metadata.Servlet;
import org.jboss.shrinkwrap.api.Archive;
import org.jboss.shrinkwrap.api.ArchivePaths;
import org.jboss.shrinkwrap.api.ShrinkWrap;
import org.jboss.shrinkwrap.api.asset.StringAsset;
import org.jboss.shrinkwrap.api.spec.EnterpriseArchive;
import org.jboss.shrinkwrap.api.spec.JavaArchive;
import org.jboss.shrinkwrap.api.spec.ResourceAdapterArchive;
import org.jboss.shrinkwrap.api.spec.WebArchive;

/**
 * The Servlet protocol: what it packages, what it advertises, and how it calls.
 *
 * Three seams, and all three are reached through the interfaces the rest of
 * Arquillian reaches them through. A container adapter picks a {@code Protocol}
 * out of the registry and asks it for a {@code DeploymentPackager} and a
 * {@code ContainerMethodExecutor}; the deployment chain asks the registered
 * {@code AuxiliaryArchiveAppender}s for their archives. Nothing here calls a
 * non-public method and nothing here needs a container.
 *
 * <p>The two protocol versions are driven through the <em>same</em> fixture
 * matrix, id for id — {@code pack25-ear-war} against {@code pack3-ear-war} — so
 * that where they agree and where they diverge is readable off the transcripts
 * rather than asserted in a comment. They diverge more than their shared
 * superclass suggests: 2.5 ships a {@code .war} and merges web descriptors, 3.0
 * ships a {@code .jar} with a {@code web-fragment.xml} and merges nothing.
 *
 * <p>The servlet API itself is never named in this source. See
 * {@link arqprobe.fam.servlet.Http} for
 * why, and for what is done instead.
 */
public class ServletFixture implements Fixture
{
   /** A web.xml the application under test brought with it. */
   private static final String APP_WEB_XML =
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         + "<web-app version=\"2.5\" xmlns=\"http://java.sun.com/xml/ns/javaee\"\n"
         + "   xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
         + "   xsi:schemaLocation=\"http://java.sun.com/xml/ns/javaee"
         + " http://java.sun.com/xml/ns/javaee/web-app_2_5.xsd\">\n"
         + "   <display-name>Application Under Test</display-name>\n"
         + "   <servlet>\n"
         + "      <servlet-name>AppServlet</servlet-name>\n"
         + "      <servlet-class>com.example.AppServlet</servlet-class>\n"
         + "   </servlet>\n"
         + "   <servlet-mapping>\n"
         + "      <servlet-name>AppServlet</servlet-name>\n"
         + "      <url-pattern>/app</url-pattern>\n"
         + "   </servlet-mapping>\n"
         + "</web-app>\n";

   /** An application.xml the application under test brought with it. */
   private static final String APP_APPLICATION_XML =
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         + "<application version=\"5\" xmlns=\"http://java.sun.com/xml/ns/javaee\"\n"
         + "   xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
         + "   xsi:schemaLocation=\"http://java.sun.com/xml/ns/javaee"
         + " http://java.sun.com/xml/ns/javaee/application_5.xsd\">\n"
         + "   <display-name>Application Under Test</display-name>\n"
         + "   <module>\n"
         + "      <ejb>beans.jar</ejb>\n"
         + "   </module>\n"
         + "</application>\n";

   private final String name;

   public ServletFixture(String name, File inputs)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      Log.reset();
      boolean known;
      try
      {
         known = pack(r) || append(r) || proto(r) || uri(r) || exec(r);
      }
      finally
      {
         r.list("log", Log.lines());
      }
      if (!known)
      {
         throw new IllegalArgumentException("unknown servlet fixture: " + name);
      }
   }

   // ---------------------------------------------------------------- packagers

   /**
    * The deployment packagers, both versions over one matrix.
    *
    * An id is {@code pack25-<case>} or {@code pack3-<case>}; the case is decided
    * once and handed whichever packager the prefix named.
    */
   private boolean pack(Rec r) throws Throwable
   {
      DeploymentPackager packager;
      String kase;
      if (name.startsWith("pack25-"))
      {
         packager = new org.jboss.arquillian.protocol.servlet.v_2_5.ServletProtocolDeploymentPackager();
         kase = name.substring("pack25-".length());
      }
      else if (name.startsWith("pack3-"))
      {
         packager = new org.jboss.arquillian.protocol.servlet.v_3.ServletProtocolDeploymentPackager();
         kase = name.substring("pack3-".length());
      }
      else
      {
         return false;
      }

      Archive<?> application;
      Collection<Archive<?>> auxiliary = new ArrayList<Archive<?>>();
      List<ProtocolArchiveProcessor> processors = new ArrayList<ProtocolArchiveProcessor>();

      if ("war".equals(kase))
      {
         application = war();
      }
      else if ("war-webxml".equals(kase))
      {
         application = war().setWebXML(new StringAsset(APP_WEB_XML));
      }
      else if ("war-aux".equals(kase))
      {
         application = war();
         auxiliary.add(aux("support-one.jar"));
         auxiliary.add(aux("support-two.jar"));
      }
      else if ("war-processors".equals(kase))
      {
         application = war();
         processors.add(new Stubs.Processor("first"));
         processors.add(new Stubs.Processor("second"));
      }
      else if ("war-processor-throws".equals(kase))
      {
         // The packager does not guard the call. A processor that fails takes
         // the whole packaging down, after the archive has already been
         // modified — so the transcript records the exception AND the state the
         // half-finished archive was left in.
         application = war();
         processors.add(new Stubs.Processor("first"));
         processors.add(new Stubs.Processor("second", new IllegalStateException("processor said no")));
         processors.add(new Stubs.Processor("third"));
      }
      else if ("jar".equals(kase))
      {
         application = jar("test.jar");
      }
      else if ("jar-aux".equals(kase))
      {
         application = jar("test.jar");
         auxiliary.add(aux("support-one.jar"));
      }
      else if ("jar-processors".equals(kase))
      {
         // A jar application is the one shape neither packager can deploy as it
         // stands, so both synthesize a container for it — and the archive the
         // processors are handed is that synthesized thing, not the jar the
         // caller passed in. Which synthesized thing differs by version, and
         // only a registered processor can see it: generateDeployment returns
         // the outermost archive either way.
         application = jar("test.jar");
         processors.add(new Stubs.Processor("first"));
         processors.add(new Stubs.Processor("second"));
      }
      else if ("ear-empty".equals(kase))
      {
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .addAsModule(jar("beans.jar"));
      }
      else if ("ear-empty-processors".equals(kase))
      {
         // An ear with no war in it: the packager adds a web module of its own
         // rather than attaching to one. This is the branch the "protocol
         // archive" half of the story lives in.
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .addAsModule(jar("beans.jar"));
         processors.add(new Stubs.Processor("first"));
         processors.add(new Stubs.Processor("second"));
      }
      else if ("ear-appxml".equals(kase))
      {
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .addAsModule(jar("beans.jar"))
               .setApplicationXML(new StringAsset(APP_APPLICATION_XML));
      }
      else if ("ear-war".equals(kase))
      {
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .addAsModule(war());
      }
      else if ("ear-war-webxml".equals(kase))
      {
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .addAsModule(war().setWebXML(new StringAsset(APP_WEB_XML)));
      }
      else if ("ear-war-appxml".equals(kase))
      {
         // One war present, so the "add a module" branch is not taken — and the
         // application.xml is therefore never touched. Recorded because both
         // packagers do rewrite it in the other branch, and an implementation
         // that "helpfully" rewrote it here too would be a behaviour change no
         // other fixture in this family would notice.
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .addAsModule(war())
               .setApplicationXML(new StringAsset(APP_APPLICATION_XML));
      }
      else if ("ear-war-processors".equals(kase))
      {
         // An ear with exactly one war: both packagers reach inside and work on
         // the nested war, so the processors are handed a member of the archive
         // the caller passed rather than the archive itself. Between this, the
         // jar case and the empty-ear case, every answer the question has is
         // recorded, and the plain-war fixtures supply the fourth.
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .addAsModule(war());
         processors.add(new Stubs.Processor("first"));
         processors.add(new Stubs.Processor("second"));
      }
      else if ("ear-two-wars".equals(kase))
      {
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .addAsModule(ShrinkWrap.create(WebArchive.class, "one.war"))
               .addAsModule(ShrinkWrap.create(WebArchive.class, "two.war"));
      }
      else if ("ear-war-not-archive".equals(kase))
      {
         // A .war path whose asset is a plain byte asset rather than a nested
         // archive. Both packagers count it as "one war found", fail the
         // instanceof, and then silently do nothing at all — no protocol
         // module, no processors called. That silence is the finding.
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .add(new StringAsset("not really an archive"), ArchivePaths.create("/opaque.war"));
         processors.add(new Stubs.Processor("first"));
      }
      else if ("ear-aux".equals(kase))
      {
         application = ShrinkWrap.create(EnterpriseArchive.class, "test.ear")
               .addAsModule(jar("beans.jar"));
         auxiliary.add(aux("support-one.jar"));
         auxiliary.add(aux("support-two.jar"));
      }
      else if ("rar".equals(kase))
      {
         // Not one of the three handled types, so the packager names the type it
         // was given and refuses. The message contains the implementation class
         // name, not the spec interface.
         application = ShrinkWrap.create(ResourceAdapterArchive.class, "test.rar");
      }
      else if ("full".equals(kase))
      {
         // Everything at once: a descriptor to merge into, libraries to add and
         // processors to call. The three are separate branches of the packager
         // and each has its own fixture; this one exists because they run in a
         // fixed order against the same archive and a reordering would leave
         // every single-feature fixture passing.
         application = war().setWebXML(new StringAsset(APP_WEB_XML));
         auxiliary.add(aux("support-one.jar"));
         auxiliary.add(aux("support-two.jar"));
         processors.add(new Stubs.Processor("first"));
         processors.add(new Stubs.Processor("second"));
      }
      else
      {
         return false;
      }

      r.kv("packager", packager.getClass().getName());
      r.kv("application", application == null ? "<null>" : application.getName());
      if (application != null)
      {
         r.list("application.before", Archives.tree(application));
      }
      r.sorted("auxiliary", names(auxiliary));

      final TestDeployment deployment = new TestDeployment(application, auxiliary);
      final DeploymentPackager p = packager;
      final Collection<ProtocolArchiveProcessor> procs = processors;
      Archive<?> result = null;
      try
      {
         result = p.generateDeployment(deployment, procs);
         r.kv("generate", "ok");
      }
      catch (Throwable t)
      {
         r.kv("generate", "threw");
         r.thrown("generate.ex", t);
      }

      if (result != null)
      {
         r.kv("result.name", result.getName());
         r.kv("result.type", result.getClass().getName());
         r.kv("result.isApplication", Boolean.valueOf(result == application));
         r.list("result.tree", Archives.tree(result));
      }
      // The application archive is recorded whatever happened, including after a
      // throw: the packagers mutate in place, so "what was it left like" is a
      // separate question from "what did it return".
      if (application != null)
      {
         r.list("application.after", Archives.tree(application));
         descriptors(r, "application", application);
      }
      if (result != null && result != application)
      {
         descriptors(r, "result", result);
      }
      return true;
   }

   /**
    * The descriptors an archive ended up carrying, as tag streams.
    *
    * Content and not just presence: the Servlet 2.5 packager's entire job in the
    * merge case is to add one {@code <servlet>} to a descriptor the application
    * wrote, and an implementation that replaced the descriptor instead of
    * merging into it would produce an archive with all the same paths.
    *
    * {@link Xml#tagStream} rather than the raw text, because the text is
    * whatever XML writer the descriptor library happens to use and the tree is
    * what the container reads.
    */
   private void descriptors(Rec r, String key, Archive<?> archive)
   {
      xml(r, key + ".webxml", Archives.text(archive, "/WEB-INF/web.xml"));
      xml(r, key + ".appxml", Archives.text(archive, "/META-INF/application.xml"));
      xml(r, key + ".webfragment", Archives.text(archive, "/META-INF/web-fragment.xml"));

      // The protocol archive of the 3.0 packager travels as a library, and of
      // the 2.5 packager as a module or a merge; where a nested archive is the
      // one carrying the descriptor, look inside it too.
      for (String path : new String[] {
            "/WEB-INF/lib/arquillian-protocol.jar",
            "/arquillian-protocol.war",
            "/arquillian-protocol.jar",
            "/test.war"})
      {
         Archive<?> nested = Archives.nested(archive, path);
         if (nested != null)
         {
            xml(r, key + nested(path) + ".webxml", Archives.text(nested, "/WEB-INF/web.xml"));
            xml(r, key + nested(path) + ".webfragment",
                  Archives.text(nested, "/META-INF/web-fragment.xml"));
         }
      }
   }

   private static String nested(String path)
   {
      return "." + path.substring(path.lastIndexOf('/') + 1).replace('.', '-');
   }

   private void xml(Rec r, String key, String text)
   {
      if (text == null)
      {
         return;
      }
      r.list(key, Xml.tagStream(text));
   }

   // ---------------------------------------------------------------- appenders

   /**
    * The auxiliary archive each protocol version contributes to every
    * deployment, which is how {@code ServletTestRunner} gets into the container.
    */
   private boolean append(Rec r) throws Throwable
   {
      AuxiliaryArchiveAppender appender;
      if ("append-25".equals(name))
      {
         appender = new org.jboss.arquillian.protocol.servlet.v_2_5.ProtocolDeploymentAppender();
      }
      else if ("append-3".equals(name))
      {
         appender = new org.jboss.arquillian.protocol.servlet.v_3.ProtocolDeploymentAppender();
      }
      else
      {
         return false;
      }
      Archive<?> archive = appender.createAuxiliaryArchive();
      r.kv("archive.name", archive.getName());
      r.kv("archive.type", archive.getClass().getName());
      r.list("archive.tree", Archives.tree(archive));
      descriptors(r, "archive", archive);
      return true;
   }

   // ----------------------------------------------------------------- protocol

   /** What the protocol advertises, and the executor it builds from metadata. */
   private boolean proto(Rec r) throws Throwable
   {
      if (!name.startsWith("proto-"))
      {
         return false;
      }
      String kase = name.substring("proto-".length());

      // Typed as the shared base class and not as Protocol<C>: getExecutor is
      // declared to return ContainerMethodExecutor on the interface and is
      // narrowed to ServletMethodExecutor here, and the narrowing — which is
      // what carries getBaseURI — is part of what this family records.
      BaseServletProtocol protocol;
      if (kase.startsWith("25-"))
      {
         protocol = new org.jboss.arquillian.protocol.servlet.v_2_5.ServletProtocol();
         kase = kase.substring(3);
      }
      else if (kase.startsWith("3-"))
      {
         protocol = new org.jboss.arquillian.protocol.servlet.v_3.ServletProtocol();
         kase = kase.substring(2);
      }
      else
      {
         return false;
      }

      r.kv("protocol", protocol.getClass().getName());
      r.kv("description", protocol.getDescription().getName());
      r.kv("description.equalsDefault",
            Boolean.valueOf(protocol.getDescription().equals(
                  org.jboss.arquillian.spi.client.protocol.ProtocolDescription.DEFAULT)));
      r.kv("configurationClass", protocol.getProtocolConfigurationClass().getName());
      r.kv("packager", protocol.getPackager().getClass().getName());

      if ("advertises".equals(kase))
      {
         return true;
      }

      final ServletProtocolConfiguration config = new ServletProtocolConfiguration();
      final ProtocolMetaData metaData = new ProtocolMetaData();

      // The inputs, quoted exactly as given, before anything normalises them.
      // Several of these fixtures are built to *agree* in their output: a
      // context root of "" resolves like one of "/", a context with no servlet
      // resolves like one holding a servlet under another name, and a contextRoot
      // on the configuration changes nothing at all. Recording only the outcome
      // would make those transcripts byte-identical, and an agreement nobody can
      // see is not a recorded finding — it is a duplicate. Everything below the
      // dispatch reads the objects back, so the pair (given, read-back) also
      // shows where the constructor already rewrote a value.
      final List<String> given = new ArrayList<String>();
      HTTPContext ctx = null;

      if ("executor".equals(kase))
      {
         ctx = context(given, "node.example.org", 8080, "ArquillianServletRunner", "/test");
      }
      else if ("executor-config-host".equals(kase))
      {
         config.setHost("10.0.0.7");
         ctx = context(given, "node.example.org", 8080, "ArquillianServletRunner", "/test");
      }
      else if ("executor-config-port".equals(kase))
      {
         config.setPort(Integer.valueOf(9090));
         ctx = context(given, "node.example.org", 8080, "ArquillianServletRunner", "/test");
      }
      else if ("executor-config-both".equals(kase))
      {
         config.setHost("10.0.0.7");
         config.setPort(Integer.valueOf(9090));
         ctx = context(given, "node.example.org", 8080, "ArquillianServletRunner", "/test");
      }
      else if ("executor-config-context-root".equals(kase))
      {
         // The configuration has a contextRoot property and a setter for it, and
         // determineBaseURI ignores both: the line that would read it is
         // commented out with a TODO. The servlet metadata wins, always. This
         // transcript is meant to match plain `executor` below the config lines.
         config.setContextRoot("/ignored-by-design");
         ctx = context(given, "node.example.org", 8080, "ArquillianServletRunner", "/test");
      }
      else if ("executor-root-context".equals(kase))
      {
         ctx = context(given, "node.example.org", 8080, "ArquillianServletRunner", "/");
      }
      else if ("executor-empty-context-root".equals(kase))
      {
         // Given "" where the fixture above gives "/". Servlet's constructor
         // maps both to "/", so only `given` tells the two runs apart.
         ctx = context(given, "node.example.org", 8080, "ArquillianServletRunner", "");
      }
      else if ("executor-no-context".equals(kase))
      {
         // No HTTPContext at all: the protocol refuses by name, because a
         // container that did not publish one cannot be reached over HTTP.
         given.add("no context");
      }
      else if ("executor-no-servlet".equals(kase))
      {
         ctx = new HTTPContext("node.example.org", 8080);
         given.add("context host=node.example.org port=8080 servlets=[]");
      }
      else if ("executor-other-servlet".equals(kase))
      {
         // A context that did publish a servlet, under a name the protocol is
         // not looking for. Resolves exactly like the empty context above; the
         // lookup is by name and a near miss is a miss.
         ctx = context(given, "node.example.org", 8080, "SomeOtherServlet", "/test");
      }
      else if ("executor-two-servlets".equals(kase))
      {
         // The wanted servlet is second and differently rooted, so a lookup that
         // took the first entry rather than the named one would answer /other.
         ctx = new HTTPContext("node.example.org", 8080);
         ctx.add(new Servlet("SomeOtherServlet", "/other"));
         ctx.add(new Servlet("ArquillianServletRunner", "/test"));
         given.add("context host=node.example.org port=8080"
               + " servlets=[SomeOtherServlet@/other, ArquillianServletRunner@/test]");
      }
      else
      {
         return false;
      }

      if (ctx != null)
      {
         metaData.addContext(ctx);
      }
      r.list("given", given);
      r.kv("config.host", config.getHost());
      r.kv("config.port", config.getPort());
      r.kv("config.contextRoot", config.getContextRoot());
      r.list("context.servlets",
            ctx == null ? Collections.<Object> emptyList() : ctx.getServlets());

      final BaseServletProtocol pr = protocol;
      r.value("executor", new Rec.Call()
      {
         public Object call()
         {
            return pr.getExecutor(config, metaData).getBaseURI();
         }
      });
      // The same computation through the public helper the protocol uses, so a
      // change in either is visible even if the other compensates.
      r.value("determineBaseURI", new Rec.Call()
      {
         public Object call()
         {
            return ServletUtil.determineBaseURI(
                  config, metaData, ServletMethodExecutor.ARQUILLIAN_SERVLET_NAME);
         }
      });
      return true;
   }

   // ---------------------------------------------------------------------- URI

   /** The URI arithmetic the protocol's own metadata types do. */
   private boolean uri(Rec r) throws Throwable
   {
      if (!name.startsWith("uri-"))
      {
         return false;
      }
      String kase = name.substring("uri-".length());

      if ("config-full".equals(kase) || "config-no-context-root".equals(kase)
            || "config-empty".equals(kase))
      {
         final ServletProtocolConfiguration config = new ServletProtocolConfiguration();
         if (!"config-empty".equals(kase))
         {
            config.setHost("node.example.org");
            config.setPort(Integer.valueOf(8080));
         }
         if ("config-full".equals(kase))
         {
            config.setContextRoot("test");
         }
         r.kv("host", config.getHost());
         r.kv("port", config.getPort());
         r.kv("contextRoot", config.getContextRoot());
         // getBaseURI concatenates an unconditional "/" before the context root,
         // so a context root that already starts with one produces a double
         // slash. Nothing in the tree calls this method; it is recorded because
         // it is public API and a migration is free to "fix" it.
         r.value("baseURI", new Rec.Call()
         {
            public Object call()
            {
               return config.getBaseURI();
            }
         });
         return true;
      }

      if ("servlet-clean-root".equals(kase) || "servlet-already-clean".equals(kase)
            || "servlet-empty-root".equals(kase))
      {
         String root = "servlet-clean-root".equals(kase) ? "test"
               : "servlet-already-clean".equals(kase) ? "/test" : "";
         final Servlet servlet = new Servlet("ArquillianServletRunner", root);
         // Given and read back, in that order: cleanContextRoot prepends a "/"
         // to anything that lacks one, so "test" and "/test" are the same object
         // afterwards and only the quoted input distinguishes the two runs. The
         // normalisation is the finding, and a transcript that showed only its
         // result could not state it.
         r.kv("contextRoot.asGiven", quote(root));
         r.kv("name", servlet.getName());
         r.kv("contextRoot", servlet.getContextRoot());
         r.kv("toString", servlet.toString());
         final HTTPContext ctx = new HTTPContext("node.example.org", 8080);
         r.value("baseURI.beforeAdd", new Rec.Call()
         {
            public Object call()
            {
               return servlet.getBaseURI();
            }
         });
         ctx.add(servlet);
         r.value("baseURI", new Rec.Call()
         {
            public Object call()
            {
               return servlet.getBaseURI();
            }
         });
         r.value("fullURI", new Rec.Call()
         {
            public Object call()
            {
               return servlet.getFullURI();
            }
         });
         return true;
      }

      if ("servlet-null-root".equals(kase))
      {
         r.value("construct", new Rec.Call()
         {
            public Object call()
            {
               return new Servlet("ArquillianServletRunner", null);
            }
         });
         return true;
      }

      if ("context-lookup".equals(kase))
      {
         HTTPContext ctx = new HTTPContext("node.example.org", 8080);
         ctx.add(new Servlet("One", "/one"));
         ctx.add(new Servlet("Two", "/two"));
         r.kv("host", ctx.getHost());
         r.kv("port", Integer.valueOf(ctx.getPort()));
         r.list("servlets", ctx.getServlets());
         r.kv("byName.One", ctx.getServletByName("One"));
         r.kv("byName.Missing", ctx.getServletByName("Missing"));
         ProtocolMetaData meta = new ProtocolMetaData().addContext(ctx);
         r.kv("hasContext.HTTPContext", Boolean.valueOf(meta.hasContext(HTTPContext.class)));
         r.kv("hasContext.String", Boolean.valueOf(meta.hasContext(String.class)));
         r.kv("getContext.HTTPContext", meta.getContext(HTTPContext.class));
         r.kv("getContext.String", meta.getContext(String.class));
         return true;
      }
      return false;
   }

   // --------------------------------------------------------- method executor

   /**
    * {@code ServletMethodExecutor} against a real server on loopback.
    *
    * The executor's return value is a {@code TestResult} and its only other
    * observable is a throwable, but almost everything it decides happens on the
    * wire: which URL it composed, which query parameters it sent, whether it
    * retried a 404, whether it followed a redirect, how it read the body. A
    * fixture that only checked the returned result would pass against a rewrite
    * that dropped {@code outputMode=serializedObject} — the value the servlet
    * runner switches on — so every case here records the request the server saw
    * alongside the answer the executor produced.
    *
    * <p>Three things are deliberately absent from these transcripts. The port,
    * because {@code Rec.norm} folds it away. The timestamps on a
    * {@code TestResult}, because its constructor calls
    * {@code System.currentTimeMillis()}. And the retry count of the fixture that
    * never gets a 200, because that count is the wall clock divided by a sleep.
    */
   private boolean exec(Rec r) throws Throwable
   {
      if (!name.startsWith("exec-"))
      {
         return false;
      }
      String kase = name.substring("exec-".length());

      if ("null-executor".equals(kase))
      {
         // Guarded before any URL is built, so this one needs no server.
         final ServletMethodExecutor ex =
               new ServletMethodExecutor(URI.create("http://127.0.0.1:1/test"));
         r.kv("baseURI", ex.getBaseURI());
         // r.call, not r.value, so this transcript reads the same way as every
         // other exec fixture's — the outcome key has the same vocabulary
         // whether a result came back or a throwable did.
         r.call("invoke", new Rec.Call()
         {
            public Object call()
            {
               return ex.invoke(null);
            }
         });
         return true;
      }

      if ("unreachable".equals(kase))
      {
         // Nothing is listening, so the connect fails rather than the protocol.
         // The cause is the only one in this group whose text the JVM composes
         // and then changed — see Rec.thrownOuterMessage for the measurement.
         URI base = URI.create("http://127.0.0.1:" + Http.deadPort() + "/test");
         invoke(r, base, new Stubs.MethodExecutor(
               new Stubs.Case(), Stubs.Case.class.getMethod("shouldPass")), true);
         return true;
      }

      if ("file-url".equals(kase))
      {
         // A base URI that is not http at all. openConnection does no IO for a
         // file: URL, so the executor's own instanceof check is what refuses it.
         invoke(r, URI.create("file:/no/such/deployment"), new Stubs.MethodExecutor(
               new Stubs.Case(), Stubs.Case.class.getMethod("shouldPass")), false);
         return true;
      }

      Http.Responder responder;
      String root = "/test";
      Object instance = new Stubs.Case();
      Method method = Stubs.Case.class.getMethod("shouldPass");
      boolean pinnedCount = true;

      if ("passed".equals(kase))
      {
         responder = ok(new TestResult(TestResult.Status.PASSED));
      }
      else if ("skipped".equals(kase))
      {
         responder = ok(new TestResult(TestResult.Status.SKIPPED));
      }
      else if ("failed".equals(kase))
      {
         // A throwable with the (String, Throwable) constructor the result's
         // ExceptionProxy reaches for first, so it arrives as itself.
         method = Stubs.Case.class.getMethod("shouldFail");
         responder = ok(new TestResult(TestResult.Status.FAILED,
               new Stubs.Failure("the test said no",
                     new IllegalArgumentException("and here is why"))));
      }
      else if ("failed-unconstructable".equals(kase))
      {
         // No constructor the proxy knows how to call: the client gets an
         // ArquillianProxyException naming the original instead of the original.
         method = Stubs.Case.class.getMethod("shouldFail");
         responder = ok(new TestResult(TestResult.Status.FAILED,
               new Stubs.Unconstructable(17)));
      }
      else if ("not-testresult".equals(kase))
      {
         // A well-formed serialized object of the wrong type. The executor
         // interpolates it into its complaint, so its toString reaches the text.
         responder = body(200, Http.serialized("a String, not a result"));
      }
      else if ("garbage".equals(kase))
      {
         responder = body(200, "not a serialized object at all".getBytes("UTF-8"));
      }
      else if ("empty-body".equals(kase))
      {
         // 200 with nothing behind it: the ObjectInputStream constructor is what
         // fails, before anything has been read.
         responder = body(200, new byte[0]);
      }
      else if ("status-500".equals(kase))
      {
         responder = body(500, new byte[0]);
      }
      else if ("status-204".equals(kase))
      {
         // A 2xx that is not 200. The executor tests for equality with 200, not
         // for the family, so this is an error and not an empty result.
         responder = body(204, new byte[0]);
      }
      else if ("redirect".equals(kase))
      {
         // HttpURLConnection follows redirects unless told not to, and the
         // executor never tells it not to. So a 302 is not an error: the
         // redirected GET happens underneath and its body is what gets read.
         // Two requests reach the server and only the first is the executor's.
         final byte[] result = Http.serialized(new TestResult(TestResult.Status.PASSED));
         responder = new Http.Responder()
         {
            public Http.Reply reply(int seq)
            {
               return seq == 1
                     ? new Http.Reply(302, null, "/elsewhere")
                     : new Http.Reply(200, result);
            }
         };
      }
      else if ("404-once".equals(kase))
      {
         // One miss and then the answer, which pins the request count at two.
         responder = Http.missThen(1, new Http.Reply(
               200, Http.serialized(new TestResult(TestResult.Status.PASSED))));
      }
      else if ("404-forever".equals(kase))
      {
         responder = Http.always(new Http.Reply(404, null));
         pinnedCount = false;
      }
      else if ("trailing-slash".equals(kase))
      {
         // The executor concatenates its servlet mapping onto the base URI with
         // no separator logic at all, so a base that ends in "/" produces a
         // doubled one. Whether the server minds is the server's business; what
         // this records is that the client sent it.
         root = "/test/";
         responder = ok(new TestResult(TestResult.Status.PASSED));
      }
      else if ("nested-class".equals(kase))
      {
         // A '$' in the class name, unescaped, straight into a query string.
         instance = new Stubs.Case.Nested();
         method = Stubs.Case.Nested.class.getMethod("shouldPass");
         responder = ok(new TestResult(TestResult.Status.PASSED));
      }
      else if ("method-with-arguments".equals(kase))
      {
         // Only the method's name crosses the wire; its signature does not, so
         // an overload is not distinguishable at the far end.
         method = Stubs.Case.class.getMethod("takesArguments", String.class, Integer.TYPE);
         responder = ok(new TestResult(TestResult.Status.PASSED));
      }
      else
      {
         return false;
      }

      Http http = pinnedCount
            ? Http.start(responder) : Http.startLoggingFirstOnly(responder);
      try
      {
         invoke(r, URI.create(http.base(root)),
               new Stubs.MethodExecutor(instance, method), false);
      }
      finally
      {
         http.stop();
      }
      for (String line : http.seen())
      {
         Log.add(line);
      }
      if (pinnedCount)
      {
         r.kv("requests", Integer.valueOf(http.requests()));
      }
      else
      {
         // Deliberately a boolean. This executor retries for a wall-clock second
         // sleeping 200 ms per miss, so the count is four or five depending on
         // how loaded the machine is. What the fixture is actually asking is
         // whether it retried at all, and that does not move.
         r.kv("requests.retried", Boolean.valueOf(http.requests() > 1));
      }
      return true;
   }

   /**
    * Drive one invocation and record the base URI, the result and the failure.
    *
    * @param jdkComposedCause whether the throwable underneath the executor's own
    *        is the JVM talking about its own machinery, in which case its text
    *        is suppressed and its type kept — true for exactly one case here
    */
   private void invoke(Rec r, URI base, final Stubs.MethodExecutor testMethodExecutor,
         boolean jdkComposedCause)
   {
      final ServletMethodExecutor executor = new ServletMethodExecutor(base);
      r.kv("baseURI", executor.getBaseURI());
      r.kv("testClass", testMethodExecutor.getInstance().getClass().getName());
      r.kv("testMethod", testMethodExecutor.getMethod().getName());
      Object result;
      try
      {
         result = executor.invoke(testMethodExecutor);
         r.kv("invoke", "ok");
      }
      catch (Throwable t)
      {
         r.kv("invoke", "threw");
         if (jdkComposedCause)
         {
            r.thrownOuterMessage("invoke.ex", t);
         }
         else
         {
            r.thrown("invoke.ex", t);
         }
         return;
      }
      if (!(result instanceof TestResult))
      {
         r.kv("result", result);
         return;
      }
      TestResult testResult = (TestResult) result;
      // Status and failure only. getStart and getEnd are set from
      // System.currentTimeMillis() in the constructor and can never be recorded.
      r.kv("result.status", testResult.getStatus());
      r.kv("result.hasExceptionProxy",
            Boolean.valueOf(testResult.getExceptionProxy() != null));
      if (testResult.getExceptionProxy() != null)
      {
         r.kv("result.exceptionProxy.hasException",
               Boolean.valueOf(testResult.getExceptionProxy().hasException()));
      }
      // Rebuilt by reflection from a class name, a message and a stack trace —
      // the type chain and the messages are the contract, the frames are not,
      // and Rec.thrown records only the former.
      r.thrown("result.throwable", testResult.getThrowable());
   }

   private static Http.Responder ok(TestResult result) throws java.io.IOException
   {
      return Http.always(new Http.Reply(200, Http.serialized(result)));
   }

   private static Http.Responder body(int status, byte[] bytes)
   {
      return Http.always(new Http.Reply(status, bytes));
   }

   // ------------------------------------------------------------------ helpers

   /**
    * One context holding one servlet, quoting the context root it was given.
    *
    * {@code Servlet}'s constructor cleans the context root on the way in, so by
    * the time anything can read the object back the difference between {@code ""}
    * and {@code "/"}, or between {@code "test"} and {@code "/test"}, is gone. The
    * quoted string is the only place a transcript can show what was asked for.
    */
   private static HTTPContext context(
         List<String> given, String host, int port, String servletName, String contextRoot)
   {
      given.add("context host=" + host + " port=" + port
            + " servlet=" + servletName + " contextRoot(as given)=" + quote(contextRoot));
      return new HTTPContext(host, port).add(new Servlet(servletName, contextRoot));
   }

   /** A string with its boundaries visible, so "" and "/" cannot read alike. */
   private static String quote(String s)
   {
      return s == null ? "<null>" : "\"" + s + "\"";
   }

   private static WebArchive war()
   {
      return ShrinkWrap.create(WebArchive.class, "test.war")
            .addAsWebResource(new StringAsset("<html>under test</html>"), "index.html");
   }

   private static JavaArchive jar(String name)
   {
      return ShrinkWrap.create(JavaArchive.class, name)
            .addAsManifestResource(new StringAsset("Name: " + name + "\n"), "under-test.txt");
   }

   private static JavaArchive aux(String name)
   {
      return ShrinkWrap.create(JavaArchive.class, name)
            .addAsManifestResource(new StringAsset("Name: " + name + "\n"), "support.txt");
   }

   private static List<String> names(Collection<Archive<?>> archives)
   {
      List<String> out = new ArrayList<String>();
      for (Archive<?> a : archives)
      {
         out.add(a.getName());
      }
      return out;
   }
}
