package arqprobe.fam;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.ObjectInputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import org.jboss.arquillian.protocol.servlet.runner.ServletTestRunner;
import org.jboss.arquillian.spi.TestResult;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.webrunner.Log;
import arqprobe.fam.webrunner.Runners;
import arqprobe.fam.webrunner.Samples;
import arqprobe.fam.webrunner.Servlets;

/**
 * The server side of the Servlet protocol: {@code ServletTestRunner} and the
 * {@code SecurityActions} it leans on.
 *
 * <h3>What is being driven</h3>
 *
 * One {@code doGet}. It reads three request parameters, loads a class by name
 * through the thread context loader, asks {@code TestRunners} for a runner
 * through the same loader, runs one method and writes the answer out in one of
 * two modes. Every branch of that is reachable from two dynamic proxies; see
 * {@link Servlets} for why there is no container here and why no fixture ever
 * writes the servlet package name down.
 *
 * <h3>Four things the recordings pin that are not obviously intended</h3>
 *
 * <ul>
 * <li>The html branch never renders a result. The template has two
 * {@code Method}/{@code Status} table headers and no rows, and the source says
 * {@code // TODO}. {@code html-result-is-discarded} records a failing run and a
 * passing run producing byte-identical bodies.</li>
 * <li>{@code doGet} catches {@code Exception}. An {@code Error} from the runner
 * leaves the servlet with no status set and nothing written
 * ({@code runner-error-escapes}).</li>
 * <li>{@code writeObject} sets the status after writing the object, at which
 * point zero bytes have actually reached the stream, and only then flushes.
 * The log line carries the count so the order is visible
 * ({@code serial-status-after-write}).</li>
 * <li>The class is loaded before the runner is looked up, which is the opposite
 * order from {@code JMXTestRunner}. {@code shipped-order} records a run with
 * both broken and gets {@code ClassNotFoundException}, not
 * {@code NoSuchElementException}.</li>
 * </ul>
 *
 * <h3>What is deliberately not written down</h3>
 *
 * Neither the servlet package name (see {@link Servlets#servletName}) nor the
 * size of any serialized payload that carries a stack trace. {@code TestResult}
 * marshals its throwable through an {@code ExceptionProxy} that holds the
 * frames, and a frame count is a property of the JDK rather than of the tree.
 * Where a throwable is serialized the transcript records the stream magic, the
 * fact that the payload is non-empty, and the rebuilt exception chain -- all
 * three stable -- and not the byte count.
 */
public class WebrunnerFixture implements Fixture
{
   private static final String SAMPLE = Samples.Sample.class.getName();

   private static final String NESTED = Samples.Nested.Inner.class.getName();

   /** Parameter names, taken from the servlet rather than repeated by hand. */
   private static final String CLASS = ServletTestRunner.PARA_CLASS_NAME;

   private static final String METHOD = ServletTestRunner.PARA_METHOD_NAME;

   private static final String MODE = ServletTestRunner.PARA_OUTPUT_MODE;

   private static final String SERIALIZED = ServletTestRunner.OUTPUT_MODE_SERIALIZED;

   private static final String HTML = ServletTestRunner.OUTPUT_MODE_HTML;

   private final String name;

   public WebrunnerFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      Log.reset();
      if (!(shippedFixture(r) || paramFixture(r) || htmlFixture(r) || serialFixture(r)
            || runnerFixture(r) || securityFixture(r) || shapeFixture(r)))
      {
         throw new IllegalArgumentException("unknown webrunner fixture: " + name);
      }
   }

   // ------------------------------------------------------------------------
   // what the class path as shipped actually does
   // ------------------------------------------------------------------------

   private boolean shippedFixture(Rec r) throws Throwable
   {
      if (!name.startsWith("shipped-"))
      {
         return false;
      }
      if ("shipped-html".equals(name))
      {
         // No TestRunner is registered anywhere on this class path, so the
         // lookup ends in NoSuchElementException -- whose message is null, and
         // sendError is handed that null verbatim.
         get(r, null, request(SAMPLE, "ok", null));
      }
      else if ("shipped-serialized".equals(name))
      {
         payload(r, get(r, null, request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("shipped-order".equals(name))
      {
         // Both halves broken at once. The failure that arrives says which one
         // the servlet reached first: the class load, not the runner lookup.
         payload(r, get(r, null, request("com.example.NotHere", "ok", SERIALIZED)), false);
      }
      else
      {
         return false;
      }
      return true;
   }

   // ------------------------------------------------------------------------
   // the three request parameters
   // ------------------------------------------------------------------------

   private boolean paramFixture(Rec r) throws Throwable
   {
      if (!name.startsWith("param-"))
      {
         return false;
      }
      ClassLoader one = Runners.providing(Runners.Recording.class);
      if ("param-defaults-to-html".equals(name))
      {
         body(r, get(r, one, request(SAMPLE, "ok", null)));
      }
      else if ("param-output-mode-html".equals(name))
      {
         body(r, get(r, one, request(SAMPLE, "ok", HTML)));
      }
      else if ("param-output-mode-serialized".equals(name))
      {
         payload(r, get(r, one, request(SAMPLE, "ok", SERIALIZED)), true);
      }
      else if ("param-output-mode-serialized-uppercase".equals(name))
      {
         // equalsIgnoreCase, so the mode is case-insensitive on the way in --
         // but the html default is spelled exactly, so only this one direction
         // is forgiving.
         payload(r, get(r, one, request(SAMPLE, "ok", "SERIALIZEDOBJECT")), true);
      }
      else if ("param-output-mode-unknown".equals(name))
      {
         body(r, get(r, one, request(SAMPLE, "ok", "xml")));
      }
      else if ("param-output-mode-empty".equals(name))
      {
         body(r, get(r, one, request(SAMPLE, "ok", "")));
      }
      else if ("param-no-class-name".equals(name))
      {
         get(r, one, request(null, "ok", HTML));
      }
      else if ("param-no-class-name-serialized".equals(name))
      {
         payload(r, get(r, one, request(null, "ok", SERIALIZED)), false);
      }
      else if ("param-no-method-name".equals(name))
      {
         get(r, one, request(SAMPLE, null, HTML));
      }
      else if ("param-no-method-name-serialized".equals(name))
      {
         payload(r, get(r, one, request(SAMPLE, null, SERIALIZED)), false);
      }
      else if ("param-empty-class-name".equals(name))
      {
         // "" is not null, so the guard lets it past and the class loader
         // decides. Whether that is a ClassNotFoundException is the loader's
         // business, and worth having written down.
         payload(r, get(r, one, request("", "ok", SERIALIZED)), false);
      }
      else if ("param-empty-method-name".equals(name))
      {
         // Nothing between the guard and the runner looks at the method name.
         payload(r, get(r, one, request(SAMPLE, "", SERIALIZED)), true);
      }
      else if ("param-unknown-class".equals(name))
      {
         payload(r, get(r, one, request("com.example.NotHere", "ok", SERIALIZED)), false);
      }
      else
      {
         return false;
      }
      return true;
   }

   // ------------------------------------------------------------------------
   // the html branch
   // ------------------------------------------------------------------------

   private boolean htmlFixture(Rec r) throws Throwable
   {
      if (!name.startsWith("html-"))
      {
         return false;
      }
      if ("html-body".equals(name))
      {
         body(r, get(r, Runners.providing(Runners.Recording.class), request(SAMPLE, "ok", HTML)));
      }
      else if ("html-content-type".equals(name))
      {
         // Recorded through the log rather than a getter, because Servlet 2.5's
         // response has no getContentType to ask.
         get(r, Runners.providing(Runners.Recording.class), request(SAMPLE, "ok", HTML));
      }
      else if ("html-result-is-discarded".equals(name))
      {
         Servlets.Response passing = run(Runners.providing(Runners.Recording.class),
               request(SAMPLE, "ok", HTML));
         Servlets.Response failing = run(Runners.providing(Runners.Failing.class),
               request(SAMPLE, "ok", HTML));
         r.kv("bodies.identical", Boolean.valueOf(passing.text().equals(failing.text())));
         r.kv("body.length", Integer.valueOf(passing.text().length()));
         r.kv("mentions.status.word", Boolean.valueOf(passing.text().contains("FAILED")
               || passing.text().contains("PASSED")));
         log(r);
      }
      else if ("html-never-closes-the-document".equals(name))
      {
         Servlets.Response resp = run(Runners.providing(Runners.Recording.class),
               request(SAMPLE, "ok", HTML));
         String text = resp.text();
         r.kv("opens.html", Boolean.valueOf(text.contains("<html>")));
         r.kv("closes.body", Boolean.valueOf(text.contains("</body>")));
         r.kv("closes.html", Boolean.valueOf(text.contains("</html>")));
         r.kv("tables", Integer.valueOf(count(text, "<table>")));
         r.kv("rows", Integer.valueOf(count(text, "<tr>")));
      }
      else if ("html-writer-throws".equals(name))
      {
         // Inside the try, so it becomes an ordinary failure -- and the failure
         // path for html is sendError, which needs no writer.
         get(r, Runners.providing(Runners.Recording.class), request(SAMPLE, "ok", HTML),
               new Servlets.Response().failWriter());
      }
      else if ("html-send-error-throws".equals(name))
      {
         // The catch block is not itself guarded, so this one leaves doGet.
         get(r, Runners.providing(Runners.Recording.class), request(null, "ok", HTML),
               new Servlets.Response().failSendError());
      }
      else
      {
         return false;
      }
      return true;
   }

   // ------------------------------------------------------------------------
   // the serialized branch
   // ------------------------------------------------------------------------

   private boolean serialFixture(Rec r) throws Throwable
   {
      if (!name.startsWith("serial-"))
      {
         return false;
      }
      if ("serial-passed".equals(name))
      {
         // The one payload with no throwable in it, so the one whose size is a
         // property of the tree rather than of the JDK's stack frames.
         payload(r, get(r, Runners.providing(Runners.Recording.class),
               request(SAMPLE, "ok", SERIALIZED)), true);
      }
      else if ("serial-stream-header".equals(name))
      {
         Servlets.Response resp = run(Runners.providing(Runners.Recording.class),
               request(SAMPLE, "ok", SERIALIZED));
         byte[] bytes = resp.bytes();
         r.kv("magic", Rec.hex(Arrays.copyOfRange(bytes, 0, Math.min(4, bytes.length))));
      }
      else if ("serial-failed".equals(name))
      {
         payload(r, get(r, Runners.providing(Runners.Failing.class), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("serial-runner-throws".equals(name))
      {
         payload(r, get(r, Runners.providing(Runners.Throwing.class), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("serial-null-result".equals(name))
      {
         // Nothing between the runner and the stream checks for null, so a null
         // result is serialized as a null and the client gets one back.
         payload(r, get(r, Runners.providing(Runners.Nulling.class), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("serial-unmarshallable".equals(name))
      {
         payload(r, get(r, Runners.providing(Runners.Unmarshallable.class), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("serial-status-after-write".equals(name))
      {
         // Not "how many bytes", which is the size of a serialized stack trace
         // and therefore the JDK's business, but "all of them or none of them",
         // which is the tree's. writeObject drains the whole object before it
         // decides on a status code, so by the time the servlet calls setStatus
         // a real container would already have committed the response and the
         // call would be a no-op.
         Servlets.Response resp = get(r, Runners.providing(Runners.Recording.class),
               request(SAMPLE, "ok", SERIALIZED));
         r.kv("status.was.set", Boolean.valueOf(resp.sizeAtStatus() >= 0));
         r.kv("bytes.at.status.zero", Boolean.valueOf(resp.sizeAtStatus() == 0));
         r.kv("bytes.at.status.is.final", Boolean.valueOf(resp.sizeAtStatus() == resp.bytes().length));
      }
      else if ("serial-output-stream-throws".equals(name))
      {
         get(r, Runners.providing(Runners.Recording.class), request(SAMPLE, "ok", SERIALIZED),
               new Servlets.Response().failOutputStream());
      }
      else if ("serial-write-fails".equals(name))
      {
         get(r, Runners.providing(Runners.Recording.class), request(SAMPLE, "ok", SERIALIZED),
               new Servlets.Response().failWriteAfter(0));
      }
      else if ("serial-write-and-send-error-fail".equals(name))
      {
         // writeObject's nested catch. Reached twice: once for the result, and
         // once for the failure the first attempt became.
         get(r, Runners.providing(Runners.Recording.class), request(SAMPLE, "ok", SERIALIZED),
               new Servlets.Response().failWriteAfter(0).failSendError());
      }
      else
      {
         return false;
      }
      return true;
   }

   // ------------------------------------------------------------------------
   // where the runner comes from
   // ------------------------------------------------------------------------

   private boolean runnerFixture(Rec r) throws Throwable
   {
      if (!name.startsWith("runner-"))
      {
         return false;
      }
      if ("runner-recording".equals(name))
      {
         payload(r, get(r, Runners.providing(Runners.Recording.class), request(SAMPLE, "ok", SERIALIZED)), true);
      }
      else if ("runner-nested-test-class".equals(name))
      {
         payload(r, get(r, Runners.providing(Runners.Recording.class), request(NESTED, "ok", SERIALIZED)), true);
      }
      else if ("runner-two-providers".equals(name))
      {
         payload(r, get(r, Runners.providing(Runners.Recording.class, Runners.AlsoRecording.class),
               request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("runner-same-class-twice".equals(name))
      {
         // The provider set holds instances, not classes, so one name twice is
         // two providers and trips the "only one allowed" guard.
         payload(r, get(r, Runners.providing(Runners.Recording.class, Runners.Recording.class),
               request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("runner-missing-class".equals(name))
      {
         // Every per-line failure in the service loader is caught, printed and
         // skipped, so a bad name leaves an empty set rather than an error.
         payload(r, get(r, Runners.providing("com.example.NoSuchRunner\n"), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("runner-not-a-runner".equals(name))
      {
         payload(r, get(r, Runners.providing(Runners.NotARunner.class), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("runner-no-default-ctor".equals(name))
      {
         payload(r, get(r, Runners.providing(Runners.NeedsAnArgument.class), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("runner-no-service-file".equals(name))
      {
         payload(r, get(r, Runners.providing((String) null), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("runner-empty-service-file".equals(name))
      {
         payload(r, get(r, Runners.providing(""), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("runner-error-escapes".equals(name))
      {
         payload(r, get(r, Runners.providing(Runners.Erroring.class), request(SAMPLE, "ok", SERIALIZED)), false);
      }
      else if ("runner-uses-context-loader".equals(name))
      {
         // Both runs use the same servlet instance class and the same request;
         // only the context loader differs, and the log says which ran.
         run(Runners.providing(Runners.Recording.class), request(SAMPLE, "ok", HTML));
         List<String> first = Log.lines();
         Log.reset();
         run(Runners.providing(Runners.AlsoRecording.class), request(SAMPLE, "ok", HTML));
         r.list("first", first);
         r.list("second", Log.lines());
      }
      else
      {
         return false;
      }
      return true;
   }

   // ------------------------------------------------------------------------
   // SecurityActions
   // ------------------------------------------------------------------------

   private boolean securityFixture(Rec r) throws Throwable
   {
      if (!name.startsWith("security-"))
      {
         return false;
      }
      if ("security-tccl".equals(name))
      {
         final ClassLoader marker = Runners.empty();
         final Object[] seen = new Object[1];
         Servlets.withTccl(marker, new Runnable()
         {
            public void run()
            {
               seen[0] = quietly("getThreadContextClassLoader", new Class<?>[0]);
            }
         });
         r.kv("same.loader", Boolean.valueOf(seen[0] == marker));
         r.kv("class", seen[0] == null ? "<null>" : seen[0].getClass().getName());
      }
      else if ("security-tccl-null".equals(name))
      {
         final Object[] seen = new Object[] {"<not-run>"};
         Servlets.withTccl(null, new Runnable()
         {
            public void run()
            {
               seen[0] = quietly("getThreadContextClassLoader", new Class<?>[0]);
            }
         });
         r.kv("returned", seen[0] == null ? "<null>" : seen[0].getClass().getName());
      }
      else if ("security-no-instantiation".equals(name))
      {
         r.attempt("construct", new Rec.Body()
         {
            public void run() throws Throwable
            {
               Servlets.construct();
            }
         });
      }
      else if ("security-get-constructor".equals(name))
      {
         Object c = Servlets.security("getConstructor", ctorSig(), Samples.Pair.class,
               new Class<?>[] {String.class, int.class});
         r.kv("found", ((Constructor<?>) c).getName());
         r.list("parameters", names(((Constructor<?>) c).getParameterTypes()));
      }
      else if ("security-get-constructor-missing".equals(name))
      {
         r.attempt("get", new Rec.Body()
         {
            public void run() throws Throwable
            {
               Servlets.security("getConstructor", ctorSig(), Samples.Pair.class, new Class<?>[0]);
            }
         });
      }
      else if ("security-get-constructor-null-class".equals(name))
      {
         // Unchecked, so the privileged wrapper does not touch it and the
         // unwrapping branch below the try is never reached.
         r.attempt("get", new Rec.Body()
         {
            public void run() throws Throwable
            {
               Servlets.security("getConstructor", ctorSig(), null, new Class<?>[0]);
            }
         });
      }
      else if ("security-new-instance".equals(name))
      {
         Object made = newInstance(Samples.Plain.class.getName(), new Class<?>[0], new Object[0],
               Samples.Plain.class);
         r.kv("made", String.valueOf(made));
         r.kv("class", made.getClass().getName());
      }
      else if ("security-new-instance-with-arguments".equals(name))
      {
         Object made = newInstance(Samples.Pair.class.getName(),
               new Class<?>[] {String.class, int.class}, new Object[] {"left", Integer.valueOf(3)},
               Samples.Pair.class);
         r.kv("made", String.valueOf(made));
      }
      else if ("security-new-instance-null-class-name".equals(name))
      {
         reject(r, null, new Class<?>[0], new Object[0]);
      }
      else if ("security-new-instance-null-argument-types".equals(name))
      {
         reject(r, Samples.Plain.class.getName(), null, new Object[0]);
      }
      else if ("security-new-instance-null-arguments".equals(name))
      {
         reject(r, Samples.Plain.class.getName(), new Class<?>[0], null);
      }
      else if ("security-new-instance-unknown-class".equals(name))
      {
         reject(r, "com.example.NotHere", new Class<?>[0], new Object[0]);
      }
      else if ("security-new-instance-no-such-constructor".equals(name))
      {
         reject(r, Samples.Hidden.class.getName(), new Class<?>[0], new Object[0]);
      }
      else if ("security-new-instance-non-public-class".equals(name))
      {
         // A public constructor on a class the caller's package cannot see:
         // getConstructor finds it, newInstance cannot use it.
         //
         // Recorded with thrownOuterMessage rather than thrown, because the
         // cause's message is composed by the JVM and it moved: JDK 8 says
         // "class X can not access a member of class Y with modifiers ..." and
         // later JDKs say "cannot". The sentence this codebase owns -- the
         // RuntimeException it wraps everything in -- is recorded in full.
         try
         {
            newInstance(Samples.class.getPackage().getName() + ".PackagePrivate", new Class<?>[0],
                  new Object[0], Samples.Plain.class);
            r.kv("make", "ok");
         }
         catch (Throwable t)
         {
            r.kv("make", "threw");
            r.thrownOuterMessage("make.ex", t);
         }
      }
      else if ("security-new-instance-constructor-throws".equals(name))
      {
         reject(r, Samples.Explodes.class.getName(), new Class<?>[0], new Object[0]);
      }
      else if ("security-new-instance-wrong-expected-type".equals(name))
      {
         // Past construction, so the message names both types and the failure
         // is a ClassCastException rather than the RuntimeException above.
         r.attempt("make", new Rec.Body()
         {
            public void run() throws Throwable
            {
               newInstance(Samples.Unrelated.class.getName(), new Class<?>[0], new Object[0],
                     Samples.Plain.class);
            }
         });
      }
      else if ("security-new-instance-uses-context-loader".equals(name))
      {
         // Class.forName(className, false, tccl): a context loader that can
         // find nothing turns a class that is plainly present into a miss.
         final Object[] outcome = new Object[1];
         Servlets.withTccl(Runners.empty(), new Runnable()
         {
            public void run()
            {
               try
               {
                  outcome[0] = newInstance(Samples.Plain.class.getName(), new Class<?>[0],
                        new Object[0], Samples.Plain.class);
               }
               catch (Throwable t)
               {
                  outcome[0] = t;
               }
            }
         });
         if (outcome[0] instanceof Throwable)
         {
            r.kv("made", "<threw>");
            r.thrown("ex", (Throwable) outcome[0]);
         }
         else
         {
            r.kv("made", String.valueOf(outcome[0]));
         }
      }
      else
      {
         return false;
      }
      return true;
   }

   // ------------------------------------------------------------------------
   // declarations
   // ------------------------------------------------------------------------

   private boolean shapeFixture(Rec r) throws Throwable
   {
      if (!name.startsWith("shape-"))
      {
         return false;
      }
      if ("shape-servlet".equals(name))
      {
         shape(r, "servlet", ServletTestRunner.class);
         r.kv("servlet.superclass", Servlets.servletName(
               ServletTestRunner.class.getSuperclass().getName()));
      }
      else if ("shape-security-actions".equals(name))
      {
         shape(r, "security", Servlets.securityActions());
         r.kv("security.modifiers", Modifier.toString(Servlets.securityActions().getModifiers()));
         r.kv("security.constructors", Integer.valueOf(
               Servlets.securityActions().getDeclaredConstructors().length));
         r.kv("security.public.methods", Integer.valueOf(
               Servlets.securityActions().getMethods().length - Object.class.getMethods().length));
      }
      else if ("shape-constants".equals(name))
      {
         // The four names a client has to spell correctly for the protocol to
         // work at all, plus the two output modes.
         r.kv("PARA_CLASS_NAME", CLASS);
         r.kv("PARA_METHOD_NAME", METHOD);
         r.kv("PARA_OUTPUT_MODE", MODE);
         r.kv("OUTPUT_MODE_SERIALIZED", SERIALIZED);
         r.kv("OUTPUT_MODE_HTML", HTML);
      }
      else
      {
         return false;
      }
      return true;
   }

   // ------------------------------------------------------------------------
   // helpers
   // ------------------------------------------------------------------------

   private static Servlets.Request request(String className, String methodName, String mode)
   {
      return new Servlets.Request().with(CLASS, className).with(METHOD, methodName)
            .with(MODE, mode);
   }

   /** Drive one request and record what came back out; the ordinary shape. */
   private Servlets.Response get(Rec r, ClassLoader tccl, Servlets.Request req) throws Throwable
   {
      return get(r, tccl, req, new Servlets.Response());
   }

   private Servlets.Response get(Rec r, ClassLoader tccl, Servlets.Request req,
         final Servlets.Response resp) throws Throwable
   {
      final ClassLoader loader = tccl;
      final Object request = req.proxy();
      final Object response = resp.proxy();
      r.attempt("doGet", new Rec.Body()
      {
         public void run() throws Throwable
         {
            Servlets.doGet(loader, request, response);
         }
      });
      log(r);
      return resp;
   }

   /** Drive one request and record nothing; for fixtures that compare two runs. */
   private static Servlets.Response run(ClassLoader tccl, Servlets.Request req) throws Throwable
   {
      Servlets.Response resp = new Servlets.Response();
      Servlets.doGet(tccl, req.proxy(), resp.proxy());
      return resp;
   }

   private static void log(Rec r)
   {
      r.list("log", Log.lines());
   }

   /** The html body, line by line, so a template change shows up as a line. */
   private static void body(Rec r, Servlets.Response resp)
   {
      String text = resp.text();
      r.kv("body.length", Integer.valueOf(text.length()));
      r.list("body", Arrays.asList(text.split("\n", -1)));
      r.kv("bytes.size", Integer.valueOf(resp.bytes().length));
   }

   /**
    * Deserialize whatever reached the stream.
    *
    * @param sized whether the byte count may be recorded -- true only when the
    *              payload is known to carry no stack trace, because
    *              {@code ExceptionProxy} marshals frames and a frame count is a
    *              property of the JDK, not of the tree
    */
   private static void payload(Rec r, Servlets.Response resp, boolean sized) throws Exception
   {
      byte[] bytes = resp.bytes();
      r.kv("bytes.empty", Boolean.valueOf(bytes.length == 0));
      if (bytes.length == 0)
      {
         return;
      }
      if (sized)
      {
         r.kv("bytes.size", Integer.valueOf(bytes.length));
      }
      Object read = new ObjectInputStream(new ByteArrayInputStream(bytes)).readObject();
      if (read == null)
      {
         r.kv("payload", "<null>");
         return;
      }
      r.kv("payload.class", read.getClass().getName());
      if (!(read instanceof TestResult))
      {
         return;
      }
      TestResult result = (TestResult) read;
      r.kv("payload.status", result.getStatus());
      Throwable t = result.getThrowable();
      if (t == null)
      {
         r.kv("payload.throwable", "<null>");
      }
      else
      {
         r.thrown("payload.throwable", t);
      }
   }

   private static int count(String haystack, String needle)
   {
      int n = 0;
      for (int at = haystack.indexOf(needle); at >= 0; at = haystack.indexOf(needle, at + 1))
      {
         n++;
      }
      return n;
   }

   private static List<String> names(Class<?>[] types)
   {
      List<String> out = new ArrayList<String>();
      for (int i = 0; i < types.length; i++)
      {
         out.add(types[i].getName());
      }
      return out;
   }

   private static Class<?>[] ctorSig()
   {
      return new Class<?>[] {Class.class, Class[].class};
   }

   private static Class<?>[] newInstanceSig()
   {
      return new Class<?>[] {String.class, Class[].class, Object[].class, Class.class};
   }

   private static Object newInstance(String className, Class<?>[] types, Object[] args,
         Class<?> expected) throws Throwable
   {
      return Servlets.security("newInstance", newInstanceSig(), className, types, args, expected);
   }

   /** A newInstance call whose whole content is the failure it produces. */
   private static void reject(Rec r, final String className, final Class<?>[] types,
         final Object[] args)
   {
      r.attempt("make", new Rec.Body()
      {
         public void run() throws Throwable
         {
            newInstance(className, types, args, Samples.Plain.class);
         }
      });
   }

   /** For use inside a Runnable, where a checked throwable has nowhere to go. */
   private static Object quietly(String method, Class<?>[] signature, Object... args)
   {
      try
      {
         return Servlets.security(method, signature, args);
      }
      catch (Throwable t)
      {
         return t;
      }
   }

   /** Declarations, folded so that the servlet package rename is invisible. */
   private static void shape(Rec r, String key, Class<?> c)
   {
      List<String> ifaces = new ArrayList<String>();
      for (Class<?> i : c.getInterfaces())
      {
         ifaces.add(Servlets.servletName(i.getName()));
      }
      Collections.sort(ifaces);
      r.list(key + ".interfaces", ifaces);
      List<String> methods = new ArrayList<String>();
      for (Method m : c.getDeclaredMethods())
      {
         // Declarations only. javac's own methods -- the lambda$... bodies it
         // writes when an anonymous class becomes a lambda, and bridges -- are
         // not part of the shape a rename would take away, and this fixture
         // measured two of them the moment the Java 8 step converted
         // SecurityActions. Same line as Util.compilerMade draws over $1.class.
         if (m.isSynthetic())
         {
            continue;
         }
         StringBuilder sb = new StringBuilder(Modifier.toString(m.getModifiers()))
               .append(' ').append(Servlets.servletName(m.getReturnType().getName()))
               .append(' ').append(m.getName()).append('(');
         Class<?>[] ps = m.getParameterTypes();
         for (int i = 0; i < ps.length; i++)
         {
            sb.append(i == 0 ? "" : ",").append(Servlets.servletName(ps[i].getName()));
         }
         methods.add(sb.append(')').toString());
      }
      Collections.sort(methods);
      r.list(key + ".methods", methods);
   }
}
