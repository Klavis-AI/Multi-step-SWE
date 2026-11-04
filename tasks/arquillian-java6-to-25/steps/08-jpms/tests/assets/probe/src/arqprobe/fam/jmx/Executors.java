package arqprobe.fam.jmx;

import java.lang.reflect.Method;

import org.jboss.arquillian.spi.TestMethodExecutor;

/**
 * What the client side hands {@code JMXMethodExecutor}.
 *
 * The executor reads exactly two things off a {@code TestMethodExecutor} -- the
 * runtime class of its instance and the name of its method -- turns them into
 * two strings, and sends those over JMX. It never calls {@code invoke}, which
 * is the point of a remote protocol: the method runs in the container, not
 * here. {@link Stub#invoked} is how a fixture proves that, and would stop
 * proving it the moment the executor started running things locally.
 */
public final class Executors
{
   private Executors()
   {
   }

   /** The class whose name travels over the wire. */
   public static class Sample
   {
      public void ok()
      {
      }

      public void alsoOk()
      {
      }
   }

   /** A sample in a nested class, so the name that travels carries a {@code $}. */
   public static class Nested
   {
      public static class Deeper
      {
         public void ok()
         {
         }
      }
   }

   public static Method method(Class<?> owner, String name) throws Exception
   {
      return owner.getMethod(name);
   }

   /** The ordinary case: an instance and one of its methods. */
   public static final class Stub implements TestMethodExecutor
   {
      private final Object instance;
      private final Method method;

      /** Set if the executor ever runs the method here instead of sending it. */
      public boolean invoked;

      public Stub(Object instance, Method method)
      {
         this.instance = instance;
         this.method = method;
      }

      public Method getMethod()
      {
         return method;
      }

      public Object getInstance()
      {
         return instance;
      }

      public void invoke(Object... parameters)
      {
         invoked = true;
         Log.add("invoke locally");
      }
   }

   /** Answers null where the executor does not check, so the fixture can record where it lands. */
   public static final class Empty implements TestMethodExecutor
   {
      private final boolean nullInstance;

      public Empty(boolean nullInstance)
      {
         this.nullInstance = nullInstance;
      }

      public Method getMethod()
      {
         if (nullInstance)
         {
            try
            {
               return Sample.class.getMethod("ok");
            }
            catch (NoSuchMethodException e)
            {
               throw new IllegalStateException(e);
            }
         }
         return null;
      }

      public Object getInstance()
      {
         return nullInstance ? null : new Sample();
      }

      public void invoke(Object... parameters)
      {
      }
   }

   /** Throws from the accessors themselves, which sit outside the executor's try block. */
   public static final class Hostile implements TestMethodExecutor
   {
      public Method getMethod()
      {
         throw new IllegalStateException("no method is available");
      }

      public Object getInstance()
      {
         return new Sample();
      }

      public void invoke(Object... parameters)
      {
      }
   }
}
