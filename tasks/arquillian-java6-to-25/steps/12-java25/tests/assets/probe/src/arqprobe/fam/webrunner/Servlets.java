package arqprobe.fam.webrunner;

import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.LinkedHashMap;
import java.util.Map;

import org.jboss.arquillian.protocol.servlet.runner.ServletTestRunner;

/**
 * A servlet container made of two {@link Proxy} instances.
 *
 * <h3>Why there is no container here</h3>
 *
 * {@code ServletTestRunner.doGet} takes an {@code HttpServletRequest} and an
 * {@code HttpServletResponse}, and both are interfaces. It reads three
 * parameters off the first and calls at most five methods on the second. So the
 * whole server side of the Servlet protocol can be driven by two dynamic
 * proxies and a stub output stream, with no deployment, no connector and no
 * port -- which also means no wall-clock, no free port to pick and nothing to
 * shut down, the three things that make container fixtures non-deterministic.
 *
 * <h3>Why the package name is never written down</h3>
 *
 * The servlet API is one of the packages that is renamed on the way up the
 * ladder. This file therefore never contains the string that {@code run.sh}
 * rewrites: the namespace is read off the tree at runtime -- from the superclass
 * of {@code ServletTestRunner}, which is whichever {@code HttpServlet} that tree
 * was compiled against -- and every name that reaches a transcript goes through
 * {@link #servletName}, which folds both spellings to {@code <servlet>}. A
 * recording made before the rename therefore still matches after it, which is
 * the difference between a family that survives step 13 and one that has to be
 * deleted when it arrives.
 *
 * <p>The one thing that cannot be handled this way is {@code ServletOutputStream},
 * which is abstract, and abstract in two different shapes. That lives in
 * {@code probe/src-ns/}; see {@code Out.java}.
 */
public final class Servlets
{
   /**
    * Split so that neither half is the token {@code run.sh} rewrites. The sed
    * anchors on {@code javax.servlet} as a whole word; {@code "javax"} on its
    * own is not that, and must not be, because this class has to name both
    * namespaces at once in order to fold them together.
    */
   private static final String SERVLET_PKG = ".servlet.";

   private static final String SECURITY_ACTIONS =
         "org.jboss.arquillian.protocol.servlet.runner.SecurityActions";

   /** {@code javax} or {@code jakarta}, read off the tree rather than assumed. */
   public static final String NS = namespace();

   private Servlets()
   {
   }

   private static String namespace()
   {
      String parent = ServletTestRunner.class.getSuperclass().getName();
      int at = parent.indexOf(SERVLET_PKG);
      if (at < 0)
      {
         throw new IllegalStateException("ServletTestRunner does not extend a servlet: " + parent);
      }
      return parent.substring(0, at);
   }

   /** A servlet API type by its name below the {@code servlet} package. */
   public static Class<?> type(String tail) throws ClassNotFoundException
   {
      return Class.forName(NS + SERVLET_PKG + tail, false, Servlets.class.getClassLoader());
   }

   /**
    * Fold both spellings of the servlet package to one token.
    *
    * Applied to every class or member name that reaches a transcript. Without
    * it, {@code servlet-shape} would record {@code doGet(javax.servlet.…)}
    * today and {@code doGet(jakarta.servlet.…)} after the rename, and the
    * family would fail the step that did the renaming -- for being right.
    */
   public static String servletName(String s)
   {
      return s.replace("javax" + SERVLET_PKG, "<servlet>.")
            .replace("jakarta" + SERVLET_PKG, "<servlet>.");
   }

   // ------------------------------------------------------------------------
   // the request
   // ------------------------------------------------------------------------

   /**
    * Answers {@code getParameter} from a map and records every call.
    *
    * A parameter that was never set and a parameter set to null are the same
    * thing to a real request, and to this one.
    */
   public static final class Request implements InvocationHandler
   {
      private final Map<String, String> params = new LinkedHashMap<String, String>();

      private Object proxy;

      public Request with(String key, String value)
      {
         params.put(key, value);
         return this;
      }

      public Object proxy() throws ClassNotFoundException
      {
         if (proxy == null)
         {
            proxy = Proxy.newProxyInstance(Servlets.class.getClassLoader(),
                  new Class<?>[] {type("http.HttpServletRequest")}, this);
         }
         return proxy;
      }

      public Object invoke(Object p, Method m, Object[] args) throws Throwable
      {
         if ("getParameter".equals(m.getName()))
         {
            String key = (String) args[0];
            Log.add("getParameter " + key);
            return params.get(key);
         }
         return Servlets.other(p, m, args);
      }
   }

   // ------------------------------------------------------------------------
   // the response
   // ------------------------------------------------------------------------

   /**
    * Records every call in order, keeps whatever was written, and can be told
    * to fail at each of the three points the servlet has an opinion about.
    */
   public static final class Response implements InvocationHandler
   {
      private final Out out = new Out();

      private final StringWriter buffer = new StringWriter();

      private final PrintWriter writer = new PrintWriter(buffer);

      private boolean failOutputStream;

      private boolean failWriter;

      private boolean failSendError;

      private Object proxy;

      /** How much had reached the stream when setStatus was called; -1 if never. */
      private int sizeAtStatus = -1;

      /** {@code getOutputStream} throws, before any byte is written. */
      public Response failOutputStream()
      {
         this.failOutputStream = true;
         return this;
      }

      /** {@code getWriter} throws, which the html branch has no answer for. */
      public Response failWriter()
      {
         this.failWriter = true;
         return this;
      }

      /** {@code sendError} throws, which is what reaches the nested catch. */
      public Response failSendError()
      {
         this.failSendError = true;
         return this;
      }

      /** Writing fails at the given offset; 0 means the very first byte. */
      public Response failWriteAfter(int bytes)
      {
         out.failAfter(bytes);
         return this;
      }

      public Object proxy() throws ClassNotFoundException
      {
         if (proxy == null)
         {
            proxy = Proxy.newProxyInstance(Servlets.class.getClassLoader(),
                  new Class<?>[] {type("http.HttpServletResponse")}, this);
         }
         return proxy;
      }

      public byte[] bytes()
      {
         return out.bytes();
      }

      /** How much had been written when setStatus was called, or -1 if it never was. */
      public int sizeAtStatus()
      {
         return sizeAtStatus;
      }

      public String text()
      {
         writer.flush();
         return buffer.toString();
      }

      public Object invoke(Object p, Method m, Object[] args) throws Throwable
      {
         String n = m.getName();
         if ("setContentType".equals(n))
         {
            Log.add("setContentType " + args[0]);
            return null;
         }
         if ("setStatus".equals(n))
         {
            // Whether anything had been written, never how much. writeObject
            // sets the status AFTER writing the object, and by then the whole
            // payload has drained -- but its size is the size of a serialized
            // stack trace, which is a property of the JDK. The exact figure is
            // available to fixtures through sizeAtStatus(), which compares it
            // against the final size rather than recording either.
            sizeAtStatus = out.size();
            Log.add("setStatus " + args[0]
                  + (sizeAtStatus == 0 ? " (nothing written yet)" : " (output already begun)"));
            return null;
         }
         if ("sendError".equals(n))
         {
            Log.add("sendError " + args[0] + " " + (args.length > 1 ? String.valueOf(args[1]) : "<code-only>"));
            if (failSendError)
            {
               throw new IOException("the response was already committed");
            }
            return null;
         }
         if ("getWriter".equals(n))
         {
            Log.add("getWriter");
            if (failWriter)
            {
               throw new IOException("no writer is available");
            }
            return writer;
         }
         if ("getOutputStream".equals(n))
         {
            Log.add("getOutputStream");
            if (failOutputStream)
            {
               throw new IOException("no output stream is available");
            }
            return out;
         }
         return Servlets.other(p, m, args);
      }
   }

   /**
    * Anything the servlet asks for that the fixtures did not anticipate.
    *
    * It is logged rather than refused, because a proxy that threw here would
    * turn "the servlet grew a call" into a fixture crash somewhere unrelated,
    * and because the name of the new call is the thing worth knowing. The
    * three {@code Object} methods are answered without logging and without an
    * identity hash code, which is not recordable.
    */
   static Object other(Object proxy, Method m, Object[] args)
   {
      String n = m.getName();
      if ("toString".equals(n) && m.getParameterTypes().length == 0)
      {
         return "<proxy>";
      }
      if ("hashCode".equals(n) && m.getParameterTypes().length == 0)
      {
         return Integer.valueOf(0);
      }
      if ("equals".equals(n) && m.getParameterTypes().length == 1)
      {
         return Boolean.valueOf(proxy == args[0]);
      }
      Log.add("unexpected " + n);
      return zero(m.getReturnType());
   }

   private static Object zero(Class<?> t)
   {
      if (!t.isPrimitive())
      {
         return null;
      }
      if (t == boolean.class)
      {
         return Boolean.FALSE;
      }
      if (t == char.class)
      {
         return Character.valueOf('\0');
      }
      if (t == long.class)
      {
         return Long.valueOf(0L);
      }
      if (t == float.class)
      {
         return Float.valueOf(0f);
      }
      if (t == double.class)
      {
         return Double.valueOf(0d);
      }
      if (t == byte.class)
      {
         return Byte.valueOf((byte) 0);
      }
      if (t == short.class)
      {
         return Short.valueOf((short) 0);
      }
      if (t == int.class)
      {
         return Integer.valueOf(0);
      }
      return null;
   }

   // ------------------------------------------------------------------------
   // driving the servlet
   // ------------------------------------------------------------------------

   /**
    * Call {@code doGet} with the given thread context class loader.
    *
    * {@code doGet} is protected, so it is reached reflectively; the wrapper the
    * reflection adds is unwrapped, because a fixture asking what an unhandled
    * {@code Error} does wants the {@code Error}, not an
    * {@code InvocationTargetException} holding one. The context loader is
    * always restored: {@code Driver} gives each fixture its own thread, but a
    * leak would still be a fixture leaving state behind, which is the one thing
    * a replayable transcript cannot tolerate.
    */
   public static void doGet(ClassLoader tccl, Object request, Object response) throws Throwable
   {
      Method m = ServletTestRunner.class.getDeclaredMethod("doGet",
            type("http.HttpServletRequest"), type("http.HttpServletResponse"));
      m.setAccessible(true);
      Thread current = Thread.currentThread();
      ClassLoader previous = current.getContextClassLoader();
      if (tccl != null)
      {
         current.setContextClassLoader(tccl);
      }
      try
      {
         m.invoke(new ServletTestRunner(), request, response);
      }
      catch (InvocationTargetException e)
      {
         throw e.getCause();
      }
      finally
      {
         current.setContextClassLoader(previous);
      }
   }

   // ------------------------------------------------------------------------
   // SecurityActions
   // ------------------------------------------------------------------------

   /**
    * {@code SecurityActions} is a public class whose every method is
    * package-private, so nothing outside {@code …protocol.servlet.runner} can
    * call one without reflection -- which is exactly the property its javadoc
    * claims ("a set of privileged actions that are not to leak out of this
    * package") and therefore exactly the property worth pinning.
    */
   public static Class<?> securityActions() throws ClassNotFoundException
   {
      return Class.forName(SECURITY_ACTIONS, true, Servlets.class.getClassLoader());
   }

   /** Invoke one of its static methods, unwrapping the reflective wrapper. */
   public static Object security(String method, Class<?>[] signature, Object... args) throws Throwable
   {
      Method m = securityActions().getDeclaredMethod(method, signature);
      m.setAccessible(true);
      try
      {
         return m.invoke(null, args);
      }
      catch (InvocationTargetException e)
      {
         throw e.getCause();
      }
   }

   /** What the private constructor does when something reaches it anyway. */
   public static Object construct() throws Throwable
   {
      java.lang.reflect.Constructor<?> c = securityActions().getDeclaredConstructor();
      c.setAccessible(true);
      try
      {
         return c.newInstance();
      }
      catch (InvocationTargetException e)
      {
         throw e.getCause();
      }
   }

   /** Run a body with a given thread context class loader, then put it back. */
   public static void withTccl(ClassLoader tccl, Runnable body)
   {
      Thread current = Thread.currentThread();
      ClassLoader previous = current.getContextClassLoader();
      current.setContextClassLoader(tccl);
      try
      {
         body.run();
      }
      finally
      {
         current.setContextClassLoader(previous);
      }
   }
}
