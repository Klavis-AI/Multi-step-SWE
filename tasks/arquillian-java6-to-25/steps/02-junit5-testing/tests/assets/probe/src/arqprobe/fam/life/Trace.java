package arqprobe.fam.life;

import java.util.Arrays;
import java.util.Collection;
import java.util.TreeMap;

import org.jboss.arquillian.impl.client.container.event.DeployDeployment;
import org.jboss.arquillian.impl.client.container.event.DeployManagedDeployments;
import org.jboss.arquillian.impl.client.container.event.SetupContainer;
import org.jboss.arquillian.impl.client.container.event.SetupContainers;
import org.jboss.arquillian.impl.client.container.event.StartContainer;
import org.jboss.arquillian.impl.client.container.event.StartManagedContainers;
import org.jboss.arquillian.impl.client.container.event.StopContainer;
import org.jboss.arquillian.impl.client.container.event.StopManagedContainers;
import org.jboss.arquillian.impl.client.container.event.UnDeployDeployment;
import org.jboss.arquillian.impl.client.container.event.UnDeployManagedDeployments;
import org.jboss.arquillian.impl.client.deployment.event.GenerateDeployment;
import org.jboss.arquillian.impl.configuration.api.ArquillianDescriptor;
import org.jboss.arquillian.impl.configuration.api.ContainerDef;
import org.jboss.arquillian.impl.configuration.api.GroupDef;
import org.jboss.arquillian.impl.configuration.api.ProtocolDef;
import org.jboss.arquillian.impl.domain.Container;
import org.jboss.arquillian.impl.domain.ContainerRegistry;
import org.jboss.arquillian.impl.domain.ProtocolDefinition;
import org.jboss.arquillian.impl.domain.ProtocolRegistry;
import org.jboss.arquillian.impl.execution.event.LocalExecutionEvent;
import org.jboss.arquillian.impl.execution.event.RemoteExecutionEvent;
import org.jboss.arquillian.spi.Profile;
import org.jboss.arquillian.spi.client.container.DeployableContainer;
import org.jboss.arquillian.spi.client.deployment.DeploymentDescription;
import org.jboss.arquillian.spi.client.protocol.ProtocolDescription;
import org.jboss.arquillian.spi.core.Instance;
import org.jboss.arquillian.spi.core.annotation.Inject;
import org.jboss.arquillian.spi.core.annotation.Observes;
import org.jboss.arquillian.spi.event.container.AfterDeploy;
import org.jboss.arquillian.spi.event.container.AfterSetup;
import org.jboss.arquillian.spi.event.container.AfterStart;
import org.jboss.arquillian.spi.event.container.AfterStop;
import org.jboss.arquillian.spi.event.container.AfterUnDeploy;
import org.jboss.arquillian.spi.event.container.BeforeDeploy;
import org.jboss.arquillian.spi.event.container.BeforeSetup;
import org.jboss.arquillian.spi.event.container.BeforeStart;
import org.jboss.arquillian.spi.event.container.BeforeStop;
import org.jboss.arquillian.spi.event.container.BeforeUnDeploy;
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
 * An extension that writes down which events the manager fired and what each
 * one carried.
 *
 * Extensions are how a third party observes a run, so this is the codebase's
 * own surface rather than a way around it: a fixture registers this profile in
 * {@code META-INF/services} exactly as an integration would.
 *
 * Each line is appended when the event has finished being delivered, not when
 * it starts, because the manager hands an event to observers in registration
 * order and the profile Arquillian registers for itself is registered first.
 * Events the observers of a parent event fire therefore appear above their
 * parent. The order is a manager property, not a scheduling accident, and it
 * moves only if dispatch does.
 *
 * Only leaf event types are observed. Observing a base type would also catch
 * every subtype, and the two lines would then be ordered by
 * {@code Class.getMethods()}, which the JVM does not specify.
 */
public class Trace implements Profile
{
   public Collection<Class<?>> getClientProfile()
   {
      return Arrays.<Class<?>>asList(Observer.class);
   }

   public Collection<Class<?>> getContainerProfile()
   {
      return Arrays.<Class<?>>asList(Observer.class);
   }

   /**
    * A second, much smaller extension.
    *
    * Registered alongside {@link Trace} by the one fixture that asks how two
    * third-party extensions interleave with each other and with the profile
    * Arquillian registers for itself.
    */
   public static class Second implements Profile
   {
      public Collection<Class<?>> getClientProfile()
      {
         return Arrays.<Class<?>>asList(Echo.class);
      }

      public Collection<Class<?>> getContainerProfile()
      {
         return Arrays.<Class<?>>asList(Echo.class);
      }

      public static class Echo
      {
         public void beforeSuite(@Observes BeforeSuite event)
         {
            Log.add("echo:BeforeSuite");
         }

         public void beforeClass(@Observes BeforeClass event)
         {
            Log.add("echo:BeforeClass class=" + event.getTestClass().getName());
         }

         public void test(@Observes Test event)
         {
            Log.add("echo:Test method=" + event.getTestMethodExecutor().getMethod().getName());
         }
      }
   }

   /** One observer method per event, each recording what that event carries. */
   public static class Observer
   {
      @Inject
      private Instance<ArquillianDescriptor> descriptor;

      @Inject
      private Instance<ContainerRegistry> containerRegistry;

      @Inject
      private Instance<ProtocolRegistry> protocolRegistry;

      // --- suite, class and test ------------------------------------------

      public void beforeSuite(@Observes BeforeSuite event)
      {
         Log.add("event:BeforeSuite");
         registries();
      }

      public void afterSuite(@Observes AfterSuite event)
      {
         Log.add("event:AfterSuite");
      }

      public void beforeClass(@Observes BeforeClass event)
      {
         Log.add("event:BeforeClass class=" + event.getTestClass().getName()
               + " executor=" + present(event.getExecutor()));
      }

      public void afterClass(@Observes AfterClass event)
      {
         Log.add("event:AfterClass class=" + event.getTestClass().getName());
      }

      public void before(@Observes Before event)
      {
         Log.add("event:Before class=" + event.getTestClass().getName()
               + " method=" + event.getTestMethod().getName()
               + " instance=" + event.getTestInstance().getClass().getName()
               + " executor=" + present(event.getExecutor()));
      }

      public void after(@Observes After event)
      {
         Log.add("event:After method=" + event.getTestMethod().getName());
      }

      public void test(@Observes Test event)
      {
         Log.add("event:Test method=" + event.getTestMethodExecutor().getMethod().getName()
               + " instance=" + event.getTestMethodExecutor().getInstance().getClass().getName());
      }

      // --- deployment generation ------------------------------------------

      public void generateDeployment(@Observes GenerateDeployment event)
      {
         Log.add("event:GenerateDeployment class=" + event.getTestClass().getName());
      }

      // --- container lifecycle, the multi-container events -----------------

      public void setupContainers(@Observes SetupContainers event)
      {
         Log.add("event:SetupContainers");
      }

      public void startManaged(@Observes StartManagedContainers event)
      {
         Log.add("event:StartManagedContainers");
      }

      public void stopManaged(@Observes StopManagedContainers event)
      {
         Log.add("event:StopManagedContainers");
      }

      public void deployManaged(@Observes DeployManagedDeployments event)
      {
         Log.add("event:DeployManagedDeployments");
      }

      public void undeployManaged(@Observes UnDeployManagedDeployments event)
      {
         Log.add("event:UnDeployManagedDeployments");
      }

      // --- container lifecycle, one container at a time --------------------

      public void setupContainer(@Observes SetupContainer event)
      {
         Log.add("event:SetupContainer container=" + event.getContainerName());
      }

      public void startContainer(@Observes StartContainer event)
      {
         Log.add("event:StartContainer container=" + event.getContainerName());
      }

      public void stopContainer(@Observes StopContainer event)
      {
         Log.add("event:StopContainer container=" + event.getContainerName());
      }

      public void deployDeployment(@Observes DeployDeployment event)
      {
         Log.add("event:DeployDeployment container=" + event.getContainerName()
               + " deployment=" + name(event.getDeployment())
               + " adapter=" + adapter(event.getDeployableContainer()));
      }

      public void undeployDeployment(@Observes UnDeployDeployment event)
      {
         Log.add("event:UnDeployDeployment container=" + event.getContainerName()
               + " deployment=" + name(event.getDeployment()));
      }

      // --- the adapter callbacks, as the manager announces them ------------

      public void beforeSetup(@Observes BeforeSetup event)
      {
         Log.add("event:BeforeSetup adapter=" + adapter(event.getDeployableContainer()));
      }

      public void afterSetup(@Observes AfterSetup event)
      {
         Log.add("event:AfterSetup adapter=" + adapter(event.getDeployableContainer()));
      }

      public void beforeStart(@Observes BeforeStart event)
      {
         Log.add("event:BeforeStart adapter=" + adapter(event.getDeployableContainer()));
      }

      public void afterStart(@Observes AfterStart event)
      {
         Log.add("event:AfterStart adapter=" + adapter(event.getDeployableContainer()));
      }

      public void beforeStop(@Observes BeforeStop event)
      {
         Log.add("event:BeforeStop adapter=" + adapter(event.getDeployableContainer()));
      }

      public void afterStop(@Observes AfterStop event)
      {
         Log.add("event:AfterStop adapter=" + adapter(event.getDeployableContainer()));
      }

      public void beforeDeploy(@Observes BeforeDeploy event)
      {
         Log.add("event:BeforeDeploy deployment=" + name(event.getDeployment()));
      }

      public void afterDeploy(@Observes AfterDeploy event)
      {
         Log.add("event:AfterDeploy deployment=" + name(event.getDeployment()));
      }

      public void beforeUnDeploy(@Observes BeforeUnDeploy event)
      {
         Log.add("event:BeforeUnDeploy deployment=" + name(event.getDeployment()));
      }

      public void afterUnDeploy(@Observes AfterUnDeploy event)
      {
         Log.add("event:AfterUnDeploy deployment=" + name(event.getDeployment()));
      }

      // --- execution and enrichment ----------------------------------------

      public void localExecution(@Observes LocalExecutionEvent event)
      {
         Log.add("event:LocalExecutionEvent method=" + event.getExecutor().getMethod().getName());
      }

      public void remoteExecution(@Observes RemoteExecutionEvent event)
      {
         Log.add("event:RemoteExecutionEvent method=" + event.getExecutor().getMethod().getName());
      }

      public void beforeEnrichment(@Observes BeforeEnrichment event)
      {
         Log.add("event:BeforeEnrichment");
      }

      public void afterEnrichment(@Observes AfterEnrichment event)
      {
         Log.add("event:AfterEnrichment");
      }

      /**
       * What the boot produced, written down once the suite has started.
       *
       * Everything a descriptor decides — which of the declared containers are
       * registered, which properties survived the populator, which protocol
       * ended up default — is settled before the first callback fires. Two
       * descriptors can therefore differ in every way that matters and still
       * drive a byte-identical sequence of callbacks, which is exactly what
       * the first version of the boot fixtures recorded. This is the line
       * between them.
       *
       * {@code Container.createDeployableConfiguration()} is deliberately not
       * called: it re-runs {@code validate()}, and the configuration logs that.
       */
      private void registries()
      {
         ArquillianDescriptor declared = descriptor.get();
         if (declared == null)
         {
            Log.add("registry:declared <none>");
         }
         else
         {
            StringBuilder containers = new StringBuilder();
            for (ContainerDef container : declared.getContainers())
            {
               append(containers, container);
            }
            StringBuilder groups = new StringBuilder();
            for (GroupDef group : declared.getGroups())
            {
               if (groups.length() > 0)
               {
                  groups.append(',');
               }
               groups.append(group.getGroupName()).append('(');
               StringBuilder members = new StringBuilder();
               for (ContainerDef container : group.getGroupContainers())
               {
                  append(members, container);
               }
               groups.append(members).append(')');
            }
            Log.add("registry:declared containers=[" + containers + "]"
                  + " groups=[" + groups + "]"
                  + " defaultProtocol=" + (declared.getDefaultProtocol() == null
                        ? "<none>" : declared.getDefaultProtocol().getType()));
         }

         ContainerRegistry registry = containerRegistry.get();
         if (registry == null)
         {
            Log.add("registry:containers <none>");
         }
         else
         {
            for (Container container : registry.getContainers())
            {
               ContainerDef definition = container.getContainerConfiguration();
               Log.add("registry:container name=" + container.getName()
                     + " default=" + definition.isDefault()
                     + " adapter=" + container.getDeployableContainer().getClass().getName()
                     // Sorted: the descriptor hands the properties back in a
                     // HashMap, whose iteration order is not a promise.
                     + " properties=" + new TreeMap<String, String>(definition.getContainerProperties())
                     + " protocols=" + protocols(definition));
            }
            Log.add("registry:containers.size=" + registry.getContainers().size());
         }

         ProtocolRegistry protocols = protocolRegistry.get();
         ProtocolDefinition fallback = protocols == null
               ? null : protocols.getProtocol(ProtocolDescription.DEFAULT);
         Log.add("registry:defaultProtocol=" + (fallback == null ? "<none>" : fallback.getName()));
      }

      private static void append(StringBuilder sb, ContainerDef container)
      {
         if (sb.length() > 0)
         {
            sb.append(',');
         }
         sb.append(container.getContainerName()).append(container.isDefault() ? "(default)" : "");
      }

      private static String protocols(ContainerDef definition)
      {
         StringBuilder sb = new StringBuilder("[");
         for (ProtocolDef protocol : definition.getProtocols())
         {
            if (sb.length() > 1)
            {
               sb.append(',');
            }
            sb.append(protocol.getType())
                  .append(new TreeMap<String, String>(protocol.getProtocolProperties()));
         }
         return sb.append(']').toString();
      }

      private static String present(Object o)
      {
         return o == null ? "<none>" : "present";
      }

      private static String adapter(DeployableContainer<?> container)
      {
         return container == null ? "<none>" : container.getClass().getName();
      }

      private static String name(DeploymentDescription deployment)
      {
         if (deployment == null)
         {
            return "<none>";
         }
         // Target and protocol are set by the generator rather than by the
         // constructor, so an event raised on a description that never reached
         // it would find them missing. Recorded as such rather than thrown on.
         return deployment.getName()
               + " target=" + (deployment.getTarget() == null
                     ? "<none>" : deployment.getTarget().getName())
               + " order=" + deployment.getOrder()
               + " testable=" + deployment.testable()
               + " managed=" + deployment.managed()
               + " protocol=" + (deployment.getProtocol() == null
                     ? "<none>" : deployment.getProtocol().getName());
      }
   }
}
