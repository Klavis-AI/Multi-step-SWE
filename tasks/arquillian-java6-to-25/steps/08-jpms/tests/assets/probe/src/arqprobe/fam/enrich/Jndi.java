package arqprobe.fam.enrich;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Hashtable;
import java.util.LinkedHashMap;
import java.util.Map;

import javax.naming.Context;
import javax.naming.NameNotFoundException;
import javax.naming.spi.InitialContextFactory;

/**
 * A directory that answers, and says what it was asked.
 *
 * Three of the four enrichers reach the container by constructing a bare
 * {@code new InitialContext()} and looking a name up in it. With no provider
 * configured that call fails the same way every time, which hides the
 * interesting part: <em>which</em> names each enricher tries, in what order,
 * and what it does with the first one that answers. So the fixtures install a
 * provider of their own and read the sequence back out of {@link Log}.
 *
 * {@code javax.naming} is a JDK package and was not renamed by the Jakarta
 * move, so — unlike {@code javax.ejb} and friends — the names in this file are
 * the same on every rung of the ladder and {@code run.sh} leaves them alone.
 *
 * Installed by setting {@code java.naming.factory.initial}, which is a global
 * property in a JVM that runs every fixture. {@link #install} therefore always
 * writes it and {@link #uninstall} always restores what was there, so no
 * fixture can see a directory another one left behind. Contexts are looked up
 * per thread for the same reason.
 */
public final class Jndi
{
   private static final String PROPERTY = "java.naming.factory.initial";

   private static final ThreadLocal<Map<String, Object>> BOUND =
         new ThreadLocal<Map<String, Object>>();

   private static final ThreadLocal<Map<String, Throwable>> FAILING =
         new ThreadLocal<Map<String, Throwable>>();

   private Jndi()
   {
   }

   /** Install an empty directory. Returns the previous property value, or null. */
   public static String install()
   {
      BOUND.set(new LinkedHashMap<String, Object>());
      FAILING.set(new LinkedHashMap<String, Throwable>());
      String previous = System.getProperty(PROPERTY);
      System.setProperty(PROPERTY, Factory.class.getName());
      return previous;
   }

   /** Remove the directory and put back whatever provider was configured before. */
   public static void uninstall(String previous)
   {
      BOUND.remove();
      FAILING.remove();
      if (previous == null)
      {
         System.clearProperty(PROPERTY);
      }
      else
      {
         System.setProperty(PROPERTY, previous);
      }
   }

   /** Make {@code name} resolve to {@code value}. */
   public static void bind(String name, Object value)
   {
      bound().put(name, value);
   }

   /** Make a lookup of {@code name} raise {@code t} instead of answering. */
   public static void failWith(String name, Throwable t)
   {
      failing().put(name, t);
   }

   /**
    * The directory itself.
    *
    * Public and instantiable by name because that is how {@code InitialContext}
    * finds a provider: it reads the class name out of the environment and
    * constructs it reflectively.
    */
   public static final class Factory implements InitialContextFactory
   {
      public Context getInitialContext(Hashtable<?, ?> environment)
      {
         return (Context) Proxy.newProxyInstance(
               Factory.class.getClassLoader(),
               new Class<?>[] {Context.class},
               new Handler());
      }
   }

   /**
    * Answers {@code lookup} and nothing else.
    *
    * {@code Context} has thirty methods and the enrichers call one of them. A
    * proxy that refuses the rest by name is both shorter than an implementation
    * and louder: if a migration made an enricher start calling {@code list} or
    * {@code getEnvironment}, the fixture would say so rather than quietly
    * returning a default.
    */
   private static final class Handler implements InvocationHandler
   {
      public Object invoke(Object proxy, Method method, Object[] args) throws Throwable
      {
         String name = method.getName();
         if ("lookup".equals(name) && args != null && args.length == 1
               && method.getParameterTypes()[0] == String.class)
         {
            // Not String.valueOf: a @Resource setter with neither name nor
            // mappedName looks up a null, and "null" the string is a name a
            // fixture could also have asked for.
            return lookup((String) args[0]);
         }
         if ("close".equals(name))
         {
            Log.add("jndi:close");
            return null;
         }
         if ("toString".equals(name))
         {
            return "Jndi";
         }
         if ("hashCode".equals(name))
         {
            return Integer.valueOf(System.identityHashCode(proxy));
         }
         if ("equals".equals(name))
         {
            return Boolean.valueOf(proxy == args[0]);
         }
         Log.add("jndi:unexpected " + name);
         throw new UnsupportedOperationException("Jndi does not implement " + name);
      }

      private Object lookup(String name) throws Throwable
      {
         String shown = name == null ? "<null>" : name;
         Throwable planned = failing().get(name);
         if (planned != null)
         {
            Log.add("jndi:lookup " + shown + " -> " + planned.getClass().getName());
            throw planned;
         }
         Map<String, Object> bound = bound();
         if (bound.containsKey(name))
         {
            Object value = bound.get(name);
            Log.add("jndi:lookup " + shown + " -> " + describe(value));
            return value;
         }
         Log.add("jndi:lookup " + shown + " -> not-found");
         throw new NameNotFoundException(shown);
      }
   }

   /**
    * What a bound value is called in a transcript.
    *
    * Never the class name: half of what is bound is a proxy over a renamed
    * interface, and {@code $Proxy7} is both meaningless and dependent on how
    * many proxies the JVM happened to make first. Values that want to be named
    * implement {@link Named}; everything else is described by what it is worth.
    */
   public static String describe(Object value)
   {
      if (value == null)
      {
         return "<null>";
      }
      if (value instanceof Named)
      {
         return ((Named) value).label();
      }
      if (value instanceof Proxy)
      {
         InvocationHandler h = Proxy.getInvocationHandler(value);
         if (h instanceof Named)
         {
            return ((Named) h).label();
         }
         return "<proxy>";
      }
      return value.getClass().getName() + ":" + value;
   }

   /** Something a transcript can name without naming its class. */
   public interface Named
   {
      String label();
   }

   /** Unwrap the {@code InvocationTargetException} a reflective call adds. */
   static Throwable unwrap(Throwable t)
   {
      if (t instanceof InvocationTargetException && t.getCause() != null)
      {
         return t.getCause();
      }
      return t;
   }

   private static Map<String, Object> bound()
   {
      Map<String, Object> m = BOUND.get();
      if (m == null)
      {
         throw new IllegalStateException("Jndi.install() was not called on this thread");
      }
      return m;
   }

   private static Map<String, Throwable> failing()
   {
      Map<String, Throwable> m = FAILING.get();
      if (m == null)
      {
         throw new IllegalStateException("Jndi.install() was not called on this thread");
      }
      return m;
   }
}
