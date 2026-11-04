package arqprobe.fam.runner;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Arrays;

import org.testng.annotations.ITestAnnotation;

/**
 * The two annotation transformers, driven directly.
 *
 * {@code IAnnotationTransformer.transform} is handed an {@code ITestAnnotation}
 * and returns nothing: everything it does it does by calling setters on the
 * thing it was given. A run through {@code TestNGTestRunner} can show the
 * <em>effect</em> of {@code RemoveDependsOnTransformer} — {@code DependsOnMethod}
 * runs at all — but not which calls produced it, and it cannot reach
 * {@code TestDataProviderTransformer} even in principle, because nothing in the
 * tree registers that one. So the annotation is a {@link Proxy} that writes down
 * what was asked of it.
 *
 * <p><b>What a proxy can and cannot stand in for.</b> {@code ITestAnnotation} is
 * an interface, so a proxy is a real one of them as far as the transformer's
 * parameter type is concerned. But {@code TestDataProviderTransformer}'s body
 * asks {@code instanceof TestAnnotation} — a concrete TestNG internal — before
 * doing anything, and a proxy is not that. The transcript therefore records that
 * transformer's no-op arm, which is the arm the guard exists for, and not the
 * arm behind it. Reaching the arm behind it would mean naming
 * {@code org.testng.internal.annotations.TestAnnotation} in this harness, and an
 * internal is exactly the kind of name a version bump relocates — the harness
 * would stop compiling at the step that moves TestNG and take the whole suite
 * with it. The notes record the uncovered lines rather than the harness buying
 * them at that price.
 */
public final class Transformers
{
   private Transformers()
   {
   }

   /**
    * An {@code ITestAnnotation} that logs every call made to it.
    *
    * Getters return the type's zero value, which for {@code getDataProviderClass}
    * is {@code null} — the value that sends {@code TestDataProviderTransformer}
    * past its first guard and into the second.
    */
   public static ITestAnnotation recording()
   {
      InvocationHandler handler = new InvocationHandler()
      {
         public Object invoke(Object proxy, Method method, Object[] args)
         {
            Log.add("annotation:" + method.getName() + "(" + render(args) + ")");
            return zero(method.getReturnType());
         }
      };
      return (ITestAnnotation) Proxy.newProxyInstance(
            Transformers.class.getClassLoader(),
            new Class<?>[] { ITestAnnotation.class },
            handler);
   }

   /**
    * The arguments of one call, as text.
    *
    * Arrays are rendered by content rather than by identity: the whole point of
    * {@code RemoveDependsOnTransformer} is that it passes an empty array, and
    * {@code [Ljava.lang.String;@1b6d} would hide both the emptiness and the type.
    */
   private static String render(Object[] args)
   {
      if (args == null)
      {
         return "";
      }
      StringBuilder out = new StringBuilder();
      for (int i = 0; i < args.length; i++)
      {
         if (i > 0)
         {
            out.append(", ");
         }
         Object arg = args[i];
         if (arg instanceof Object[])
         {
            out.append(Arrays.deepToString((Object[]) arg));
         }
         else
         {
            out.append(arg);
         }
      }
      return out.toString();
   }

   /** The default value of a return type, so a getter never throws. */
   private static Object zero(Class<?> type)
   {
      if (!type.isPrimitive())
      {
         return null;
      }
      if (type == boolean.class)
      {
         return Boolean.FALSE;
      }
      if (type == int.class)
      {
         return Integer.valueOf(0);
      }
      if (type == long.class)
      {
         return Long.valueOf(0L);
      }
      return null;
   }

   /** Methods of known arity, for the data provider's shape question. */
   public static final class Arities
   {
      public void none()
      {
      }

      public void one(String a)
      {
      }

      public void two(String a, int b)
      {
      }
   }
}
