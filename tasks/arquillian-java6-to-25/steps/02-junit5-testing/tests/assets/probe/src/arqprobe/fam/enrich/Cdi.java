package arqprobe.fam.enrich;

import java.lang.annotation.Annotation;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;

import javax.enterprise.context.spi.CreationalContext;
import javax.enterprise.inject.spi.AnnotatedType;
import javax.enterprise.inject.spi.BeanManager;
import javax.enterprise.inject.spi.InjectionTarget;

/**
 * A bean manager that does nothing and reports everything.
 *
 * {@code CDIInjectionEnricher} hands the whole job to a {@code BeanManager} it
 * finds in JNDI: it creates a creational context, asks for an annotated type,
 * asks that for an injection target, and tells the target to inject. None of
 * those calls returns anything the enricher inspects, so the only observable is
 * the call sequence and the arguments — which is exactly what this records.
 *
 * Every CDI type here is reached through {@link Proxy} rather than implemented.
 * That is not to save typing: {@code BeanManager} gained methods between CDI
 * 1.0 and CDI 3.0, so a class that declared {@code implements BeanManager}
 * would compile against the one the tree starts on and stop compiling against
 * the one it ends on. A proxy answers whatever the interface turns out to
 * declare, and says so out loud when it is asked something new.
 *
 * The type names in this file are written {@code javax.*} and rebound to
 * {@code jakarta.*} by {@code run.sh} when the tree under test has moved; see
 * the namespace-binding block there.
 */
public final class Cdi
{
   private Cdi()
   {
   }

   /**
    * The one interesting thing {@code getInjectableReference} can be told to
    * hand back, and the failures it can be told to raise instead.
    */
   public static final class Manager
   {
      /** Answer to {@code isQualifier} for every annotation type asked about. */
      private boolean qualifiers;

      /** Value handed back for each resolved injection point. */
      private Object reference = "resolved";

      /** Raised by the named call instead of answering it. */
      private String throwFrom;

      public Manager qualifiersAreQualifiers()
      {
         this.qualifiers = true;
         return this;
      }

      public Manager reference(Object value)
      {
         this.reference = value;
         return this;
      }

      /** Make the named {@code BeanManager} or {@code InjectionTarget} call throw. */
      public Manager throwFrom(String method)
      {
         this.throwFrom = method;
         return this;
      }

      /** The {@code BeanManager} the enricher will find in JNDI. */
      public BeanManager beanManager()
      {
         return (BeanManager) proxy(BeanManager.class, new ManagerHandler(this));
      }
   }

   // ------------------------------------------------------------------------
   // handlers
   // ------------------------------------------------------------------------

   private static final class ManagerHandler extends Base
   {
      private final Manager cfg;

      ManagerHandler(Manager cfg)
      {
         super("BeanManager");
         this.cfg = cfg;
      }

      Object answer(Method method, Object[] args) throws Throwable
      {
         String name = method.getName();
         if ("createCreationalContext".equals(name))
         {
            Log.add("bm:createCreationalContext contextual=" + str(arg(args, 0)));
            raiseIf(cfg.throwFrom, name);
            return proxy(CreationalContext.class, new Base("CreationalContext"));
         }
         if ("createAnnotatedType".equals(name))
         {
            Log.add("bm:createAnnotatedType type=" + str(arg(args, 0)));
            raiseIf(cfg.throwFrom, name);
            return proxy(AnnotatedType.class, new Base("AnnotatedType"));
         }
         if ("createInjectionTarget".equals(name))
         {
            Log.add("bm:createInjectionTarget annotatedType=" + str(arg(args, 0)));
            raiseIf(cfg.throwFrom, name);
            return proxy(InjectionTarget.class, new TargetHandler(cfg));
         }
         if ("getInjectableReference".equals(name))
         {
            Log.add("bm:getInjectableReference");
            dump("ip", arg(args, 0));
            raiseIf(cfg.throwFrom, name);
            return cfg.reference;
         }
         if ("isQualifier".equals(name))
         {
            Object type = arg(args, 0);
            Log.add("bm:isQualifier " + str(type) + " -> " + cfg.qualifiers);
            raiseIf(cfg.throwFrom, name);
            return Boolean.valueOf(cfg.qualifiers);
         }
         return NOT_ANSWERED;
      }
   }

   private static final class TargetHandler extends Base
   {
      private final Manager cfg;

      TargetHandler(Manager cfg)
      {
         super("InjectionTarget");
         this.cfg = cfg;
      }

      Object answer(Method method, Object[] args) throws Throwable
      {
         if (!"inject".equals(method.getName()))
         {
            return NOT_ANSWERED;
         }
         Object instance = arg(args, 0);
         Log.add("it:inject instance=" + (instance == null ? "<null>" : instance.getClass().getName()));
         raiseIf(cfg.throwFrom, "inject");
         // Show the injection as a value on the instance rather than only as a
         // line in the log: a fixture that asserted on the log alone could not
         // tell "inject was called" from "inject had an effect".
         Field field = fieldNamed(instance, "injected");
         if (field != null)
         {
            field.setAccessible(true);
            field.set(instance, "by-cdi");
            Log.add("it:inject set injected");
         }
         return null;
      }
   }

   /**
    * The part every stub shares: identity, and a loud refusal.
    *
    * A CDI interface has dozens of methods and each stub answers a handful.
    * Everything else is recorded as unexpected and then thrown, so that a
    * migration which made an enricher reach for a method it never used before
    * shows up as a changed transcript rather than as a silent null.
    */
   private static class Base implements InvocationHandler, Jndi.Named
   {
      static final Object NOT_ANSWERED = new Object();

      private final String label;

      Base(String label)
      {
         this.label = label;
      }

      public String label()
      {
         return label;
      }

      public final Object invoke(Object proxy, Method method, Object[] args) throws Throwable
      {
         String name = method.getName();
         if ("toString".equals(name) && (args == null || args.length == 0))
         {
            return label;
         }
         if ("hashCode".equals(name) && (args == null || args.length == 0))
         {
            return Integer.valueOf(System.identityHashCode(proxy));
         }
         if ("equals".equals(name) && args != null && args.length == 1)
         {
            return Boolean.valueOf(proxy == args[0]);
         }
         Object answered = answer(method, args);
         if (answered != NOT_ANSWERED)
         {
            return answered;
         }
         Log.add(label.toLowerCase() + ":unexpected " + name);
         throw new UnsupportedOperationException(label + " does not implement " + name);
      }

      Object answer(Method method, Object[] args) throws Throwable
      {
         return NOT_ANSWERED;
      }
   }

   // ------------------------------------------------------------------------
   // reading an injection point back out
   // ------------------------------------------------------------------------

   /**
    * Everything {@code MethodParameterInjectionPoint} will say about itself.
    *
    * Read reflectively, by method name, so that this file does not have to name
    * {@code InjectionPoint} or {@code AnnotatedParameter} — and, more to the
    * point, so that a method the interface grows or loses does not stop the
    * dump from running. What is recorded is a projection, never a
    * {@code toString}: an {@code AnnotationLiteral} prints its own fully
    * qualified name, which is one of the names the Jakarta move changes.
    */
   static void dump(String prefix, Object injectionPoint)
   {
      if (injectionPoint == null)
      {
         Log.add(prefix + ":<null>");
         return;
      }
      Log.add(prefix + ".type=" + typeName(call(injectionPoint, "getType")));
      Log.add(prefix + ".member=" + memberName(call(injectionPoint, "getMember")));
      Log.add(prefix + ".bean=" + str(call(injectionPoint, "getBean")));
      Log.add(prefix + ".delegate=" + str(call(injectionPoint, "isDelegate")));
      Log.add(prefix + ".transient=" + str(call(injectionPoint, "isTransient")));
      Log.add(prefix + ".qualifiers=" + annotationNames(call(injectionPoint, "getQualifiers")));

      Object annotated = call(injectionPoint, "getAnnotated");
      if (annotated == null || annotated instanceof Failure)
      {
         Log.add(prefix + ".annotated=" + str(annotated));
         return;
      }
      Log.add(prefix + ".annotated.position=" + str(call(annotated, "getPosition")));
      Log.add(prefix + ".annotated.baseType=" + typeName(call(annotated, "getBaseType")));
      Log.add(prefix + ".annotated.typeClosure=" + typeNames(call(annotated, "getTypeClosure")));
      Log.add(prefix + ".annotated.annotations=" + annotationNames(call(annotated, "getAnnotations")));
      Log.add(prefix + ".annotated.declaringCallable=" + str(call(annotated, "getDeclaringCallable")));
      Log.add(prefix + ".annotated.hasTagged="
            + str(call(annotated, "isAnnotationPresent", Class.class, Beans.Tagged.class)));
      Log.add(prefix + ".annotated.hasDeprecated="
            + str(call(annotated, "isAnnotationPresent", Class.class, Deprecated.class)));
      Log.add(prefix + ".annotated.getTagged="
            + annotationName(call(annotated, "getAnnotation", Class.class, Beans.Tagged.class)));
   }

   /** What a call said, or a {@link Failure} standing in for how it refused. */
   private static Object call(Object target, String method, Object... typesAndArgs)
   {
      Class<?>[] types = new Class<?>[typesAndArgs.length / 2];
      Object[] args = new Object[typesAndArgs.length / 2];
      for (int i = 0; i < types.length; i++)
      {
         types[i] = (Class<?>) typesAndArgs[2 * i];
         args[i] = typesAndArgs[2 * i + 1];
      }
      try
      {
         Method m = target.getClass().getMethod(method, types);
         m.setAccessible(true);
         return m.invoke(target, args);
      }
      catch (Throwable t)
      {
         return new Failure(Jndi.unwrap(t));
      }
   }

   private static final class Failure
   {
      private final Throwable t;

      Failure(Throwable t)
      {
         this.t = t;
      }

      public String toString()
      {
         return "<threw " + t.getClass().getName() + ">";
      }
   }

   // ------------------------------------------------------------------------
   // projections
   // ------------------------------------------------------------------------

   /**
    * A type as its name.
    *
    * {@code Class.toString()} prefixes "class" or "interface", and a generic
    * type prints itself; both are stable, but only the name is what the
    * injection point is actually claiming, so only the name is recorded.
    */
   static String typeName(Object type)
   {
      if (type == null)
      {
         return "<null>";
      }
      if (type instanceof Class)
      {
         return ((Class<?>) type).getName();
      }
      if (type instanceof Type)
      {
         return String.valueOf(type);
      }
      return String.valueOf(type);
   }

   private static String typeNames(Object types)
   {
      if (!(types instanceof Collection))
      {
         return str(types);
      }
      List<String> out = new ArrayList<String>();
      for (Object t : (Collection<?>) types)
      {
         out.add(typeName(t));
      }
      Collections.sort(out);
      return out.toString();
   }

   /**
    * An annotation as its simple name.
    *
    * Never its {@code toString}: CDI's {@code @Default} prints as
    * {@code @javax.enterprise.inject.Default()} on one rung of the ladder and
    * {@code @jakarta.enterprise.inject.Default()} on the next, which is a
    * namespace change and not a behaviour change. The simple name says which
    * qualifier was chosen, which is the part that is being asserted.
    */
   private static String annotationName(Object annotation)
   {
      if (annotation == null)
      {
         return "<null>";
      }
      if (annotation instanceof Annotation)
      {
         return ((Annotation) annotation).annotationType().getSimpleName();
      }
      return String.valueOf(annotation);
   }

   private static String annotationNames(Object annotations)
   {
      if (!(annotations instanceof Collection))
      {
         return str(annotations);
      }
      List<String> out = new ArrayList<String>();
      for (Object a : (Collection<?>) annotations)
      {
         out.add(annotationName(a));
      }
      Collections.sort(out);
      return out.toString();
   }

   private static String memberName(Object member)
   {
      if (member == null)
      {
         return "<null>";
      }
      if (member instanceof Member)
      {
         Member m = (Member) member;
         return m.getDeclaringClass().getName() + "#" + m.getName();
      }
      return String.valueOf(member);
   }

   static String str(Object o)
   {
      if (o == null)
      {
         return "<null>";
      }
      if (o instanceof Class)
      {
         return ((Class<?>) o).getName();
      }
      return String.valueOf(o);
   }

   // ------------------------------------------------------------------------
   // plumbing
   // ------------------------------------------------------------------------

   private static Object proxy(Class<?> iface, InvocationHandler handler)
   {
      return Proxy.newProxyInstance(Cdi.class.getClassLoader(), new Class<?>[] {iface}, handler);
   }

   private static Object arg(Object[] args, int i)
   {
      return args == null || args.length <= i ? null : args[i];
   }

   private static void raiseIf(String planned, String method)
   {
      if (method.equals(planned))
      {
         throw new IllegalStateException("the bean manager refused " + method);
      }
   }

   private static Field fieldNamed(Object instance, String name)
   {
      if (instance == null)
      {
         return null;
      }
      for (Class<?> c = instance.getClass(); c != null && c != Object.class; c = c.getSuperclass())
      {
         try
         {
            return c.getDeclaredField(name);
         }
         catch (NoSuchFieldException e)
         {
            // keep walking
         }
      }
      return null;
   }
}
