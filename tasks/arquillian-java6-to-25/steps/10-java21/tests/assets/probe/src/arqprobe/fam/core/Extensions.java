package arqprobe.fam.core;

import java.io.IOException;

import org.jboss.arquillian.impl.core.spi.EventContext;
import org.jboss.arquillian.spi.core.Event;
import org.jboss.arquillian.spi.core.Instance;
import org.jboss.arquillian.spi.core.InstanceProducer;
import org.jboss.arquillian.spi.core.annotation.ApplicationScoped;
import org.jboss.arquillian.spi.core.annotation.ClassScoped;
import org.jboss.arquillian.spi.core.annotation.Inject;
import org.jboss.arquillian.spi.core.annotation.Observes;
import org.jboss.arquillian.spi.core.annotation.SuiteScoped;

/**
 * The extensions the {@code core} fixtures register with the manager.
 *
 * Each is a named top-level class rather than an anonymous one: the manager
 * reflects over the type, and the transcript is allowed to name it.
 */
public final class Extensions
{
   private Extensions()
   {
   }

   // --- plain observers -----------------------------------------------------

   public static class Recorder
   {
      public void onString(@Observes String event)
      {
         Trace.add("Recorder.onString:" + event);
      }
   }

   public static class SecondRecorder
   {
      public void onString(@Observes String event)
      {
         Trace.add("SecondRecorder.onString:" + event);
      }
   }

   public static class ObjectObserver
   {
      public void onAny(@Observes Object event)
      {
         Trace.add("ObjectObserver.onAny:" + event.getClass().getName());
      }
   }

   // --- precedence ----------------------------------------------------------

   public static class PrecedenceLow
   {
      public void on(@Observes(precedence = -10) String event)
      {
         Trace.add("precedence(-10)");
      }
   }

   public static class PrecedenceZero
   {
      public void on(@Observes String event)
      {
         Trace.add("precedence(0)");
      }
   }

   public static class PrecedenceHigh
   {
      public void on(@Observes(precedence = 10) String event)
      {
         Trace.add("precedence(10)");
      }
   }

   // --- event hierarchy -----------------------------------------------------

   // Deliberately one observer method per class: two matching methods on one
   // class would be ordered by Class.getMethods(), which the JDK does not
   // specify, so the trace would not be stable across the migration ladder.

   public static class BaseObserver
   {
      public void onBase(@Observes BaseEvent event)
      {
         Trace.add("onBase:" + event.tag());
      }
   }

   public static class SubObserver
   {
      public void onSub(@Observes SubEvent event)
      {
         Trace.add("onSub:" + event.tag());
      }
   }

   // --- interceptors --------------------------------------------------------

   public static class Interceptor
   {
      public void around(@Observes EventContext<String> context)
      {
         Trace.add("around.before:" + context.getEvent());
         context.proceed();
         Trace.add("around.after:" + context.getEvent());
      }
   }

   public static class InterceptorNoProceed
   {
      public void around(@Observes EventContext<String> context)
      {
         Trace.add("swallowed:" + context.getEvent());
      }
   }

   public static class InterceptorProceedTwice
   {
      public void around(@Observes EventContext<String> context)
      {
         context.proceed();
         context.proceed();
         Trace.add("proceeded-twice");
      }
   }

   // --- failure modes -------------------------------------------------------

   public static class ThrowsRuntime
   {
      public void on(@Observes String event)
      {
         Trace.add("ThrowsRuntime.entered");
         throw new IllegalStateException("boom: " + event);
      }
   }

   public static class ThrowsChecked
   {
      public void on(@Observes String event) throws IOException
      {
         Trace.add("ThrowsChecked.entered");
         throw new IOException("checked: " + event);
      }
   }

   public static class ThrowsError
   {
      public void on(@Observes String event)
      {
         Trace.add("ThrowsError.entered");
         throw new AssertionError("error: " + event);
      }
   }

   /** Observes the exception the manager re-fires when an observer throws. */
   public static class IllegalStateHandler
   {
      public void on(@Observes IllegalStateException event)
      {
         Trace.add("IllegalStateHandler:" + event.getMessage());
      }
   }

   /** Throws from the nested event, so the failure surfaces inside another observer's fire. */
   public static class ThrowsBaseEvent
   {
      public void on(@Observes BaseEvent event)
      {
         Trace.add("ThrowsBaseEvent.entered:" + event.tag());
         throw new UnsupportedOperationException("nested: " + event.tag());
      }
   }

   public static class NoDefaultConstructor
   {
      public NoDefaultConstructor(String required)
      {
      }

      public void on(@Observes String event)
      {
         Trace.add("NoDefaultConstructor.on");
      }
   }

   public static class PrivateConstructor
   {
      private PrivateConstructor()
      {
      }

      public void on(@Observes String event)
      {
         Trace.add("PrivateConstructor.on");
      }
   }

   public static class ThrowingConstructor
   {
      public ThrowingConstructor()
      {
         throw new IllegalStateException("constructor refused");
      }

      public void on(@Observes String event)
      {
         Trace.add("ThrowingConstructor.on");
      }
   }

   // --- observer method shapes ---------------------------------------------

   public static class StaticObserver
   {
      public static void on(@Observes String event)
      {
         Trace.add("StaticObserver.on:" + event);
      }
   }

   public static class PrivateObserver
   {
      @SuppressWarnings("unused")
      private void on(@Observes String event)
      {
         Trace.add("PrivateObserver.on:" + event);
      }
   }

   public static class ProtectedObserver
   {
      protected void on(@Observes String event)
      {
         Trace.add("ProtectedObserver.on:" + event);
      }
   }

   public static class ExtraParameterObserver
   {
      public void on(@Observes String event, Integer other)
      {
         Trace.add("ExtraParameterObserver.on:" + event + "/" + other);
      }
   }

   /** Second parameter is resolved from the manager, not from the event. */
   public static class ResolvedParameterObserver
   {
      public void on(@Observes String event, StringBuilder resolved)
      {
         Trace.add("ResolvedParameterObserver.on:" + event + "/" + resolved);
      }
   }

   /** Throws the manager's own wrapper type, which {@code ObserverImpl} rethrows unwrapped. */
   public static class ThrowsInvocationException
   {
      public void on(@Observes String event)
      {
         Trace.add("ThrowsInvocationException.entered");
         throw new org.jboss.arquillian.impl.core.spi.InvocationException(
               new IllegalArgumentException("inner: " + event));
      }
   }

   public static class UnannotatedMethod
   {
      public void on(String event)
      {
         Trace.add("UnannotatedMethod.on:" + event);
      }
   }

   public static class InheritedObserverParent
   {
      public void onParent(@Observes String event)
      {
         Trace.add("InheritedObserverParent.onParent:" + event);
      }
   }

   public static class InheritedObserverChild extends InheritedObserverParent
   {
      public void onChild(@Observes String event)
      {
         Trace.add("InheritedObserverChild.onChild:" + event);
      }
   }

   // --- injection -----------------------------------------------------------

   public static class InstanceConsumer
   {
      @Inject
      private Instance<StringBuilder> instance;

      public void on(@Observes String event)
      {
         StringBuilder value = instance == null ? null : instance.get();
         Trace.add("InstanceConsumer:" + (value == null ? "<null>" : value.toString()));
      }
   }

   public static class SuiteProducer
   {
      @Inject
      @SuiteScoped
      private InstanceProducer<StringBuilder> producer;

      public void on(@Observes String event)
      {
         producer.set(new StringBuilder("suite-" + event));
         Trace.add("SuiteProducer.set");
      }
   }

   public static class ClassProducer
   {
      @Inject
      @ClassScoped
      private InstanceProducer<StringBuilder> producer;

      public void on(@Observes String event)
      {
         producer.set(new StringBuilder("class-" + event));
         Trace.add("ClassProducer.set");
      }
   }

   public static class ApplicationProducer
   {
      @Inject
      @ApplicationScoped
      private InstanceProducer<StringBuilder> producer;

      public void on(@Observes String event)
      {
         producer.set(new StringBuilder("app-" + event));
         Trace.add("ApplicationProducer.set");
      }
   }

   /** Produces into a scope whose context is not registered with the manager. */
   public static class UnregisteredScopeProducer
   {
      @Inject
      @ClassScoped
      private InstanceProducer<StringBuilder> producer;

      public void on(@Observes String event)
      {
         producer.set(new StringBuilder("orphan"));
         Trace.add("UnregisteredScopeProducer.set");
      }
   }

   /** An {@code InstanceProducer} with no scope annotation: rejected while the manager is built. */
   public static class UnscopedProducer
   {
      @Inject
      private InstanceProducer<StringBuilder> producer;

      public void on(@Observes String event)
      {
         producer.set(new StringBuilder("never"));
      }
   }

   /** {@code @Inject} on a field that is neither an Instance, a producer, nor an Event. */
   public static class InjectOnPlainField
   {
      @Inject
      private String notInjectable;

      public void on(@Observes String event)
      {
         Trace.add("InjectOnPlainField:" + notInjectable);
      }
   }

   public static class EventFirer
   {
      @Inject
      private Event<BaseEvent> event;

      public void on(@Observes String trigger)
      {
         Trace.add("EventFirer.firing");
         event.fire(new BaseEvent("from-" + trigger));
         Trace.add("EventFirer.fired");
      }
   }

   /** Fires the event it observes; {@link Trace#enter} bounds the recursion. */
   public static class SelfFirer
   {
      @Inject
      private Event<String> event;

      public void on(@Observes String trigger)
      {
         if (!Trace.enter(4))
         {
            Trace.add("SelfFirer.bounded");
            return;
         }
         Trace.add("SelfFirer.depth:" + Trace.depth());
         event.fire(trigger + "'");
      }
   }

   public static class ManagerInjected
   {
      @Inject
      private Instance<org.jboss.arquillian.impl.core.spi.Manager> manager;

      public void on(@Observes String event)
      {
         Trace.add("ManagerInjected:" + (manager.get() == null ? "<null>" : "present"));
      }
   }

   /** Plain object, not an extension: the target of {@code Manager.inject}. */
   public static class InjectionTarget
   {
      @Inject
      private Instance<StringBuilder> instance;

      public Instance<StringBuilder> instance()
      {
         return instance;
      }
   }
}
