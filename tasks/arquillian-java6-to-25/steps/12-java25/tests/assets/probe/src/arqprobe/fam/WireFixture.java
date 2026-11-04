package arqprobe.fam;

import java.io.File;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.wire.Exceptions;
import arqprobe.fam.wire.Wire;

import org.jboss.arquillian.spi.ArquillianProxyException;
import org.jboss.arquillian.spi.ExceptionProxy;
import org.jboss.arquillian.spi.TestResult;
import org.jboss.arquillian.spi.client.protocol.metadata.HTTPContext;
import org.jboss.arquillian.spi.client.protocol.metadata.ProtocolMetaData;
import org.jboss.arquillian.spi.client.protocol.metadata.RMIContext;
import org.jboss.arquillian.spi.client.protocol.metadata.Servlet;

/**
 * What crosses the wire between the client and the container: the exception
 * proxy, the test result it carries, and the protocol metadata the client reads
 * back.
 *
 * Serialization is where a JDK upgrade shows up first, so these fixtures record
 * the codebase's own serialized shape alongside what survives a round trip.
 *
 * Code-driven family — the fixture name selects the scenario.
 */
public class WireFixture implements Fixture
{
   /** Every fixture uses this message, so a diff is about behavior and not about text. */
   private static final String MESSAGE = "remote failure";

   private final String name;

   public WireFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      if (!(proxyFixture(r) || wireFixture(r) || resultFixture(r) || metadataFixture(r)))
      {
         throw new IllegalArgumentException("unknown wire fixture: " + name);
      }
   }

   // ------------------------------------------------------------------------
   // ExceptionProxy: rebuilding a throwable the client may not be able to load
   // ------------------------------------------------------------------------

   private boolean proxyFixture(Rec r) throws Throwable
   {
      if ("proxy-string-throwable-ctor".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.StringThrowableCtor(
               MESSAGE, Exceptions.frozen(new IllegalArgumentException("root")))));
         return true;
      }
      if ("proxy-string-exception-ctor".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.StringExceptionCtor(
               MESSAGE, Exceptions.frozen(new IllegalArgumentException("root")))));
         return true;
      }
      if ("proxy-string-only-ctor".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.StringOnlyCtor(MESSAGE)));
         return true;
      }
      if ("proxy-no-suitable-ctor".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.NoArgCtorOnly()));
         return true;
      }
      if ("proxy-throwing-ctor".equals(name))
      {
         // The throwable itself cannot be built here, so construct the proxy from
         // a stand-in and rename the class on the wire.
         ExceptionProxy proxy = ExceptionProxy.createForException(
               Exceptions.frozen(new Exceptions.StringOnlyCtor(MESSAGE)));
         byte[] renamed = Wire.replaceString(Wire.write(proxy),
               Exceptions.StringOnlyCtor.class.getName(),
               Exceptions.ThrowingCtor.class.getName());
         rebuildFrom(r, renamed);
         return true;
      }
      if ("proxy-private-ctor".equals(name))
      {
         rebuild(r, Exceptions.frozen(Exceptions.PrivateStringCtor.of(MESSAGE)));
         return true;
      }
      if ("proxy-checked-exception".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.CheckedFailure(MESSAGE, null)));
         return true;
      }
      if ("proxy-error".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.FailureError(MESSAGE, null)));
         return true;
      }
      if ("proxy-null-message".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.StringOnlyCtor(null)));
         return true;
      }
      if ("proxy-empty-message".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.StringOnlyCtor("")));
         return true;
      }
      if ("proxy-multiline-message".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.StringOnlyCtor("line one\nline two\ttabbed")));
         return true;
      }
      if ("proxy-unicode-message".equals(name))
      {
         rebuild(r, Exceptions.frozen(new Exceptions.StringOnlyCtor("café 中文 😀")));
         return true;
      }
      if ("proxy-cause-chain".equals(name))
      {
         Throwable root = Exceptions.frozen(new IllegalStateException("root"));
         Throwable mid = Exceptions.frozen(new Exceptions.StringThrowableCtor("middle", root));
         rebuild(r, Exceptions.frozen(new Exceptions.StringThrowableCtor(MESSAGE, mid)));
         return true;
      }
      if ("proxy-deep-cause-chain".equals(name))
      {
         Throwable t = Exceptions.frozen(new IllegalStateException("depth-0"));
         for (int i = 1; i < 8; i++)
         {
            t = Exceptions.frozen(new Exceptions.StringThrowableCtor("depth-" + i, t));
         }
         rebuild(r, t);
         return true;
      }
      if ("proxy-cause-without-suitable-ctor".equals(name))
      {
         Throwable root = Exceptions.frozen(new Exceptions.NoArgCtorOnly());
         rebuild(r, Exceptions.frozen(new Exceptions.StringThrowableCtor(MESSAGE, root)));
         return true;
      }
      if ("proxy-null-throwable".equals(name))
      {
         r.kv("createForException(null)", ExceptionProxy.createForException(null));
         return true;
      }
      if ("proxy-tostring".equals(name))
      {
         ExceptionProxy proxy = ExceptionProxy.createForException(
               Exceptions.frozen(new Exceptions.StringThrowableCtor(MESSAGE,
                     Exceptions.frozen(new IllegalStateException("root")))));
         // super.toString() is Object's, so the identity hash is normalized out.
         r.kv("toString", proxy.toString());
         return true;
      }
      if ("proxy-get-cause-is-lazy".equals(name))
      {
         ExceptionProxy proxy = ExceptionProxy.createForException(
               Exceptions.frozen(new Exceptions.StringThrowableCtor(MESSAGE,
                     Exceptions.frozen(new IllegalStateException("root")))));
         Throwable first = proxy.getCause();
         Throwable second = proxy.getCause();
         r.kv("cause.type", first == null ? "<null>" : first.getClass().getName());
         r.kv("cause.message", first == null ? "<null>" : first.getMessage());
         r.kv("cause.cached", Boolean.valueOf(first == second));
         return true;
      }
      if ("proxy-create-exception-repeatable".equals(name))
      {
         ExceptionProxy proxy = ExceptionProxy.createForException(
               Exceptions.frozen(new Exceptions.StringOnlyCtor(MESSAGE)));
         Throwable first = proxy.createException();
         Throwable second = proxy.createException();
         r.kv("first.type", first.getClass().getName());
         r.kv("second.type", second.getClass().getName());
         r.kv("distinct.instances", Boolean.valueOf(first != second));
         return true;
      }
      if ("proxy-create-proxy-exception".equals(name))
      {
         ExceptionProxy proxy = ExceptionProxy.createForException(
               Exceptions.frozen(new Exceptions.StringOnlyCtor(MESSAGE)));
         ArquillianProxyException ex = proxy.createProxyException("a stated reason");
         r.kv("type", ex.getClass().getName());
         r.kv("message", ex.getMessage());
         Wire.chain(r, "chain", ex);
         Wire.frames(r, "frames", ex);
         return true;
      }
      if ("proxy-shape".equals(name))
      {
         Wire.shape(r, "ExceptionProxy", ExceptionProxy.class);
         Wire.shape(r, "ArquillianProxyException", ArquillianProxyException.class);
         Wire.shape(r, "TestResult", TestResult.class);
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // Payloads naming a type the client cannot use
   // ------------------------------------------------------------------------

   private boolean wireFixture(Rec r) throws Throwable
   {
      if ("wire-class-missing".equals(name))
      {
         rebuildRenamed(r, Exceptions.StringOnlyCtor.class.getName(),
               "arqprobe.fam.wire.NotOnThisClasspath");
         return true;
      }
      if ("wire-class-non-throwable".equals(name))
      {
         rebuildRenamed(r, Exceptions.StringOnlyCtor.class.getName(), "java.lang.StringBuilder");
         return true;
      }
      if ("wire-class-no-suitable-ctor".equals(name))
      {
         rebuildRenamed(r, Exceptions.StringOnlyCtor.class.getName(), "java.lang.Object");
         return true;
      }
      if ("wire-class-is-interface".equals(name))
      {
         rebuildRenamed(r, Exceptions.StringOnlyCtor.class.getName(), "java.lang.Runnable");
         return true;
      }
      if ("wire-class-is-abstract".equals(name))
      {
         rebuildRenamed(r, Exceptions.StringOnlyCtor.class.getName(), "java.io.InputStream");
         return true;
      }
      if ("wire-class-is-primitive-name".equals(name))
      {
         rebuildRenamed(r, Exceptions.StringOnlyCtor.class.getName(), "int");
         return true;
      }
      if ("wire-class-empty-name".equals(name))
      {
         rebuildRenamed(r, Exceptions.StringOnlyCtor.class.getName(), "");
         return true;
      }
      if ("wire-cause-class-missing".equals(name))
      {
         ExceptionProxy proxy = ExceptionProxy.createForException(
               Exceptions.frozen(new Exceptions.StringThrowableCtor(MESSAGE,
                     Exceptions.frozen(new Exceptions.StringOnlyCtor("root")))));
         byte[] renamed = Wire.replaceString(Wire.write(proxy),
               Exceptions.StringOnlyCtor.class.getName(),
               "arqprobe.fam.wire.NotOnThisClasspath");
         rebuildFrom(r, renamed);
         return true;
      }
      if ("wire-result-class-missing".equals(name))
      {
         TestResult result = frozenResult(TestResult.Status.FAILED,
               Exceptions.frozen(new Exceptions.StringOnlyCtor(MESSAGE)));
         byte[] renamed = Wire.replaceString(Wire.write(result),
               Exceptions.StringOnlyCtor.class.getName(),
               "arqprobe.fam.wire.NotOnThisClasspath");
         TestResult back = (TestResult) Wire.read(renamed);
         dumpResult(r, back);
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // TestResult
   // ------------------------------------------------------------------------

   private boolean resultFixture(Rec r) throws Throwable
   {
      if ("result-default".equals(name))
      {
         TestResult result = new TestResult();
         r.kv("status", result.getStatus());
         r.kv("throwable", result.getThrowable());
         r.kv("exceptionProxy", result.getExceptionProxy());
         // The constructor reads the wall clock, so only the relation is recorded.
         r.kv("start.positive", Boolean.valueOf(result.getStart() > 0));
         r.kv("end", Long.valueOf(result.getEnd()));
         return true;
      }
      if ("result-status-only".equals(name))
      {
         TestResult result = new TestResult(TestResult.Status.PASSED);
         r.kv("status", result.getStatus());
         r.kv("throwable", result.getThrowable());
         r.kv("exceptionProxy", result.getExceptionProxy());
         return true;
      }
      if ("result-explicit-times".equals(name))
      {
         TestResult result = new TestResult(TestResult.Status.PASSED);
         result.setStart(1000L);
         result.setEnd(1750L);
         r.kv("start", Long.valueOf(result.getStart()));
         r.kv("end", Long.valueOf(result.getEnd()));
         r.kv("duration", Long.valueOf(result.getEnd() - result.getStart()));
         return true;
      }
      if ("result-negative-times".equals(name))
      {
         TestResult result = new TestResult(TestResult.Status.PASSED);
         result.setStart(-5L);
         result.setEnd(Long.MIN_VALUE);
         r.kv("start", Long.valueOf(result.getStart()));
         r.kv("end", Long.valueOf(result.getEnd()));
         return true;
      }
      if ("result-set-status-after".equals(name))
      {
         TestResult result = new TestResult(TestResult.Status.PASSED);
         result.setStatus(TestResult.Status.SKIPPED);
         r.kv("status", result.getStatus());
         result.setStatus(null);
         r.kv("status.null", result.getStatus());
         return true;
      }
      if ("result-set-throwable-null".equals(name))
      {
         TestResult result = new TestResult(TestResult.Status.FAILED,
               Exceptions.frozen(new Exceptions.StringOnlyCtor(MESSAGE)));
         r.kv("before.proxy", result.getExceptionProxy() == null ? "<null>" : "present");
         result.setThrowable(null);
         r.kv("after.proxy", result.getExceptionProxy());
         r.kv("after.throwable", result.getThrowable());
         return true;
      }
      if ("result-throwable-not-reproxied".equals(name))
      {
         TestResult result = new TestResult(TestResult.Status.FAILED,
               Exceptions.frozen(new Exceptions.StringOnlyCtor(MESSAGE)));
         Throwable first = result.getThrowable();
         Throwable second = result.getThrowable();
         r.kv("same.instance", Boolean.valueOf(first == second));
         r.kv("type", first.getClass().getName());
         return true;
      }
      if ("result-roundtrip-passed".equals(name))
      {
         dumpResult(r, roundTripResult(TestResult.Status.PASSED, null));
         return true;
      }
      if ("result-roundtrip-skipped".equals(name))
      {
         dumpResult(r, roundTripResult(TestResult.Status.SKIPPED, null));
         return true;
      }
      if ("result-roundtrip-null-status".equals(name))
      {
         dumpResult(r, roundTripResult(null, null));
         return true;
      }
      if ("result-roundtrip-failed".equals(name))
      {
         dumpResult(r, roundTripResult(TestResult.Status.FAILED,
               Exceptions.frozen(new Exceptions.StringThrowableCtor(MESSAGE,
                     Exceptions.frozen(new IllegalStateException("root"))))));
         return true;
      }
      if ("result-roundtrip-assertion-error".equals(name))
      {
         dumpResult(r, roundTripResult(TestResult.Status.FAILED,
               Exceptions.frozen(new AssertionError("expected:<1> but was:<2>"))));
         return true;
      }
      if ("result-roundtrip-unbuildable-throwable".equals(name))
      {
         dumpResult(r, roundTripResult(TestResult.Status.FAILED,
               Exceptions.frozen(new Exceptions.NoArgCtorOnly())));
         return true;
      }
      if ("result-status-enum".equals(name))
      {
         r.list("values", java.util.Arrays.asList((Object[]) TestResult.Status.values()));
         for (TestResult.Status s : TestResult.Status.values())
         {
            r.kv("ordinal." + s.name(), Integer.valueOf(s.ordinal()));
         }
         r.value("valueOf.PASSED", () -> TestResult.Status.valueOf("PASSED"));
         r.value("valueOf.passed", () -> TestResult.Status.valueOf("passed"));
         r.value("valueOf.UNKNOWN", () -> TestResult.Status.valueOf("UNKNOWN"));
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // Protocol metadata
   // ------------------------------------------------------------------------

   private boolean metadataFixture(Rec r)
   {
      if ("meta-empty".equals(name))
      {
         ProtocolMetaData meta = new ProtocolMetaData();
         r.kv("has.HTTPContext", Boolean.valueOf(meta.hasContext(HTTPContext.class)));
         r.kv("get.HTTPContext", meta.getContext(HTTPContext.class));
         r.value("has.null", () -> Boolean.valueOf(meta.hasContext(null)));
         r.value("get.null", () -> meta.getContext(null));
         return true;
      }
      if ("meta-add-context".equals(name))
      {
         ProtocolMetaData meta = new ProtocolMetaData();
         ProtocolMetaData returned = meta.addContext(new HTTPContext("node-a", 8180));
         r.kv("returns.self", Boolean.valueOf(returned == meta));
         r.kv("has.HTTPContext", Boolean.valueOf(meta.hasContext(HTTPContext.class)));
         r.kv("has.RMIContext", Boolean.valueOf(meta.hasContext(RMIContext.class)));
         HTTPContext http = meta.getContext(HTTPContext.class);
         r.kv("host", http.getHost());
         r.kv("port", Integer.valueOf(http.getPort()));
         return true;
      }
      if ("meta-context-by-supertype".equals(name))
      {
         ProtocolMetaData meta = new ProtocolMetaData();
         meta.addContext(new HTTPContext("node-a", 8180));
         r.kv("has.Object", Boolean.valueOf(meta.hasContext(Object.class)));
         r.kv("get.Object.type", meta.getContext(Object.class).getClass().getName());
         return true;
      }
      if ("meta-first-match-wins".equals(name))
      {
         ProtocolMetaData meta = new ProtocolMetaData();
         meta.addContext(new HTTPContext("first", 1));
         meta.addContext(new HTTPContext("second", 2));
         r.kv("get.host", meta.getContext(HTTPContext.class).getHost());
         return true;
      }
      if ("meta-mixed-contexts".equals(name))
      {
         ProtocolMetaData meta = new ProtocolMetaData();
         meta.addContext(new RMIContext());
         meta.addContext(new HTTPContext("node-a", 8180));
         r.kv("get.HTTPContext.host", meta.getContext(HTTPContext.class).getHost());
         r.kv("has.RMIContext", Boolean.valueOf(meta.hasContext(RMIContext.class)));
         return true;
      }
      if ("meta-add-null-context".equals(name))
      {
         ProtocolMetaData meta = new ProtocolMetaData();
         r.attempt("addContext(null)", () -> meta.addContext(null));
         r.value("has.HTTPContext", () -> Boolean.valueOf(meta.hasContext(HTTPContext.class)));
         r.value("get.Object", () -> meta.getContext(Object.class));
         return true;
      }
      if ("http-empty".equals(name))
      {
         HTTPContext http = new HTTPContext("node-a", 8180);
         r.kv("host", http.getHost());
         r.kv("port", Integer.valueOf(http.getPort()));
         r.kv("servlets.size", Integer.valueOf(http.getServlets().size()));
         r.kv("byName.missing", http.getServletByName("nope"));
         return true;
      }
      if ("http-add-servlet".equals(name))
      {
         HTTPContext http = new HTTPContext("node-a", 8180);
         HTTPContext returned = http.add(new Servlet("ArquillianServletRunner", "/test"));
         r.kv("returns.self", Boolean.valueOf(returned == http));
         r.kv("servlets.size", Integer.valueOf(http.getServlets().size()));
         Servlet servlet = http.getServletByName("ArquillianServletRunner");
         r.kv("name", servlet.getName());
         r.kv("contextRoot", servlet.getContextRoot());
         r.kv("baseURI", servlet.getBaseURI());
         r.kv("fullURI", servlet.getFullURI());
         r.kv("toString", servlet.toString());
         return true;
      }
      if ("http-context-root-without-slash".equals(name))
      {
         Servlet servlet = new Servlet("runner", "test");
         new HTTPContext("node-a", 8180).add(servlet);
         r.kv("contextRoot", servlet.getContextRoot());
         r.kv("baseURI", servlet.getBaseURI());
         r.kv("fullURI", servlet.getFullURI());
         return true;
      }
      if ("http-context-root-root".equals(name))
      {
         Servlet servlet = new Servlet("runner", "/");
         new HTTPContext("node-a", 8180).add(servlet);
         r.kv("contextRoot", servlet.getContextRoot());
         r.kv("baseURI", servlet.getBaseURI());
         r.kv("fullURI", servlet.getFullURI());
         return true;
      }
      if ("http-context-root-empty".equals(name))
      {
         Servlet servlet = new Servlet("runner", "");
         new HTTPContext("node-a", 8180).add(servlet);
         r.kv("contextRoot", servlet.getContextRoot());
         r.kv("baseURI", servlet.getBaseURI());
         r.kv("fullURI", servlet.getFullURI());
         return true;
      }
      if ("http-context-root-trailing-slash".equals(name))
      {
         Servlet servlet = new Servlet("runner", "/test/");
         new HTTPContext("node-a", 8180).add(servlet);
         r.kv("contextRoot", servlet.getContextRoot());
         r.kv("baseURI", servlet.getBaseURI());
         r.kv("fullURI", servlet.getFullURI());
         return true;
      }
      if ("http-context-root-null".equals(name))
      {
         r.attempt("new Servlet(name, null)", () -> new Servlet("runner", null));
         return true;
      }
      if ("http-context-root-needs-escaping".equals(name))
      {
         Servlet servlet = new Servlet("runner name", "/a b");
         new HTTPContext("node-a", 8180).add(servlet);
         r.kv("contextRoot", servlet.getContextRoot());
         r.value("baseURI", () -> servlet.getBaseURI());
         r.value("fullURI", () -> servlet.getFullURI());
         return true;
      }
      if ("http-servlet-without-parent".equals(name))
      {
         Servlet servlet = new Servlet("runner", "/test");
         r.kv("name", servlet.getName());
         r.kv("contextRoot", servlet.getContextRoot());
         r.value("baseURI", () -> servlet.getBaseURI());
         r.value("fullURI", () -> servlet.getFullURI());
         return true;
      }
      if ("http-servlet-reparented".equals(name))
      {
         Servlet servlet = new Servlet("runner", "/test");
         new HTTPContext("first", 1).add(servlet);
         HTTPContext second = new HTTPContext("second", 2);
         second.add(servlet);
         r.kv("baseURI", servlet.getBaseURI());
         r.kv("first.servlets", Integer.valueOf(second.getServlets().size()));
         return true;
      }
      if ("http-two-servlets".equals(name))
      {
         HTTPContext http = new HTTPContext("node-a", 8180)
               .add(new Servlet("alpha", "/one"))
               .add(new Servlet("beta", "/two"));
         java.util.List<String> names = new java.util.ArrayList<String>();
         for (Servlet s : http.getServlets())
         {
            names.add(s.getName() + "=" + s.getFullURI());
         }
         r.list("servlets", names);
         r.kv("byName.alpha", http.getServletByName("alpha").getContextRoot());
         r.kv("byName.beta", http.getServletByName("beta").getContextRoot());
         return true;
      }
      if ("http-duplicate-servlet-names".equals(name))
      {
         HTTPContext http = new HTTPContext("node-a", 8180)
               .add(new Servlet("same", "/first"))
               .add(new Servlet("same", "/second"));
         r.kv("servlets.size", Integer.valueOf(http.getServlets().size()));
         r.kv("byName.contextRoot", http.getServletByName("same").getContextRoot());
         return true;
      }
      if ("http-null-host-or-port".equals(name))
      {
         Servlet servlet = new Servlet("runner", "/test");
         new HTTPContext(null, -1).add(servlet);
         r.value("baseURI", () -> servlet.getBaseURI());
         r.value("fullURI", () -> servlet.getFullURI());
         return true;
      }
      if ("http-servlet-name-null".equals(name))
      {
         HTTPContext http = new HTTPContext("node-a", 8180);
         http.add(new Servlet(null, "/test"));
         r.value("byName.lookup", () -> http.getServletByName("runner"));
         return true;
      }
      if ("rmi-defaults".equals(name))
      {
         RMIContext rmi = new RMIContext();
         r.kv("ip", rmi.getIp());
         r.kv("port", Integer.valueOf(rmi.getPort()));
         r.value("initialContext", () -> rmi.getInitialContext());
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // helpers
   // ------------------------------------------------------------------------

   /** Proxy a throwable, hand the proxy across the wire, and record what comes back. */
   private void rebuild(Rec r, Throwable original) throws Throwable
   {
      rebuildFrom(r, Wire.write(ExceptionProxy.createForException(original)));
   }

   private void rebuildRenamed(Rec r, String from, String to) throws Throwable
   {
      ExceptionProxy proxy = ExceptionProxy.createForException(
            Exceptions.frozen(new Exceptions.StringOnlyCtor(MESSAGE)));
      rebuildFrom(r, Wire.replaceString(Wire.write(proxy), from, to));
   }

   private void rebuildFrom(Rec r, byte[] payload) throws Throwable
   {
      ExceptionProxy proxy = (ExceptionProxy) Wire.read(payload);
      r.kv("hasException", Boolean.valueOf(proxy.hasException()));
      Object built = r.call("createException", () -> proxy.createException());
      if (built != null)
      {
         Throwable t = (Throwable) built;
         r.kv("type", t.getClass().getName());
         r.kv("message", t.getMessage());
         Wire.chain(r, "chain", t);
         Wire.frames(r, "frames", t);
      }
   }

   private static TestResult frozenResult(TestResult.Status status, Throwable t)
   {
      TestResult result = new TestResult(status, t);
      result.setStart(1000L);
      result.setEnd(1750L);
      return result;
   }

   private TestResult roundTripResult(TestResult.Status status, Throwable t) throws Throwable
   {
      return (TestResult) Wire.roundTrip(frozenResult(status, t));
   }

   private void dumpResult(Rec r, TestResult result)
   {
      r.kv("status", result.getStatus());
      r.kv("start", Long.valueOf(result.getStart()));
      r.kv("end", Long.valueOf(result.getEnd()));
      r.kv("exceptionProxy", result.getExceptionProxy() == null ? "<null>" : "present");
      Object t = r.call("getThrowable", () -> result.getThrowable());
      if (t != null)
      {
         r.kv("throwable.type", t.getClass().getName());
         Wire.chain(r, "throwable.chain", (Throwable) t);
         Wire.frames(r, "throwable.frames", (Throwable) t);
      }
   }
}
