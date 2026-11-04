package arqprobe.fam.runner;

import java.util.Arrays;
import java.util.Collection;

import org.jboss.arquillian.spi.Profile;
import org.jboss.arquillian.spi.core.annotation.Observes;
import org.jboss.arquillian.spi.event.enrichment.AfterEnrichment;
import org.jboss.arquillian.spi.event.enrichment.BeforeEnrichment;
import org.jboss.arquillian.spi.event.suite.After;
import org.jboss.arquillian.spi.event.suite.AfterClass;
import org.jboss.arquillian.spi.event.suite.AfterSuite;
import org.jboss.arquillian.spi.event.suite.Before;
import org.jboss.arquillian.spi.event.suite.BeforeClass;
import org.jboss.arquillian.spi.event.suite.BeforeSuite;
import org.jboss.arquillian.spi.event.suite.Test;

/**
 * The manager events an integration's run produces, written into the same log
 * the test bodies write into.
 *
 * Without this a transcript shows only that {@code @Before} ran before the test
 * body — which is JUnit doing its job and says nothing about the integration.
 * The question this family exists to ask is where the <em>Arquillian</em>
 * callbacks land relative to the framework's own: the JUnit integration wraps
 * each of JUnit's statements so that {@code adaptor.before(...)} fires and is
 * handed the {@code @Before} chain to run, rather than running beside it, and
 * the TestNG integration does something different again with plain
 * {@code @BeforeMethod} methods and a {@code NO_OP} executor. One interleaved
 * sequence is the only way to read that off a transcript.
 *
 * <p>Registered as a {@code Profile} in {@code META-INF/services} through the
 * class loader overlay, which is how a third party extends Arquillian — the
 * same mechanism {@code life} uses, and not a back door into the manager.
 * Only the container profile is populated: {@code TestRunner.execute} sets that
 * one, and an extension that claimed the client profile too would be making a
 * claim this family never tests.
 */
public class Events implements Profile
{
   public Collection<Class<?>> getClientProfile()
   {
      return Arrays.<Class<?>> asList();
   }

   public Collection<Class<?>> getContainerProfile()
   {
      return Arrays.<Class<?>> asList(Observer.class);
   }

   /**
    * One observer per event.
    *
    * Leaf types only, for the reason {@code life.Trace} gives: observing a base
    * type would also catch its subtypes and the two lines would then be ordered
    * by {@code Class.getMethods()}, which the JVM does not specify.
    *
    * <p>Each line is appended when the event has finished being delivered, so an
    * event fired by the observers of another appears above it.
    */
   public static class Observer
   {
      public void beforeSuite(@Observes BeforeSuite event)
      {
         Log.add("event:BeforeSuite");
      }

      public void beforeClass(@Observes BeforeClass event)
      {
         Log.add("event:BeforeClass class=" + simple(event.getTestClass().getName()));
      }

      public void before(@Observes Before event)
      {
         Log.add("event:Before method=" + event.getTestMethod().getName());
      }

      public void beforeEnrichment(@Observes BeforeEnrichment event)
      {
         Log.add("event:BeforeEnrichment");
      }

      public void afterEnrichment(@Observes AfterEnrichment event)
      {
         Log.add("event:AfterEnrichment");
      }

      public void test(@Observes Test event)
      {
         Log.add("event:Test method="
               + event.getTestMethodExecutor().getMethod().getName());
      }

      public void after(@Observes After event)
      {
         Log.add("event:After method=" + event.getTestMethod().getName());
      }

      public void afterClass(@Observes AfterClass event)
      {
         Log.add("event:AfterClass class=" + simple(event.getTestClass().getName()));
      }

      public void afterSuite(@Observes AfterSuite event)
      {
         Log.add("event:AfterSuite");
      }

      /**
       * The class name without its package.
       *
       * The package is this harness's own and adds a fixed prefix to every line;
       * the part that distinguishes one fixture's class from another's is the
       * tail. {@code $} is kept, because a nested class being nested is a fact
       * about what was handed to the framework.
       */
      private static String simple(String name)
      {
         int dot = name.lastIndexOf('.');
         return dot < 0 ? name : name.substring(dot + 1);
      }
   }
}
