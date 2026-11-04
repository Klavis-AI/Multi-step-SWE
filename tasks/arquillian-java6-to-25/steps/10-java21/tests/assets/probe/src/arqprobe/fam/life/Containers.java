package arqprobe.fam.life;

import org.jboss.arquillian.spi.ConfigurationException;
import org.jboss.arquillian.spi.client.container.ContainerConfiguration;
import org.jboss.arquillian.spi.client.container.DeployableContainer;
import org.jboss.arquillian.spi.client.container.DeploymentException;
import org.jboss.arquillian.spi.client.container.LifecycleException;
import org.jboss.arquillian.spi.client.protocol.ProtocolDescription;
import org.jboss.arquillian.spi.client.protocol.metadata.ProtocolMetaData;
import org.jboss.arquillian.spi.client.protocol.metadata.Servlet;
import org.jboss.arquillian.spi.client.protocol.metadata.HTTPContext;
import org.jboss.arquillian.spi.core.Instance;
import org.jboss.arquillian.spi.core.annotation.Inject;
import org.jboss.shrinkwrap.api.Archive;
import org.jboss.shrinkwrap.descriptor.api.Descriptor;

/**
 * The container integration the client run drives.
 *
 * Exactly one {@code DeployableContainer} may be registered — the registry asks
 * its loader for the only one — so there is one class here and its behaviour
 * comes from its configuration. That is not a workaround: an adapter reading
 * {@code arquillian.xml} properties and failing accordingly is precisely the
 * shape of a real integration, and it means a fixture selects what happens by
 * writing a configuration file rather than by reaching past the surface.
 *
 * Every callback is logged before it decides anything, so a run that fails
 * halfway still records how far it got.
 */
public final class Containers
{
   private Containers()
   {
   }

   /** The property value that makes each callback fail. */
   public static final String FAIL = "fail";

   /**
    * Configuration for {@link Stub}.
    *
    * {@code mode*} properties are read by the container; the rest exist so that
    * the populator's type conversion is exercised by a real configuration file
    * rather than only by a synthetic map.
    */
   public static class StubConfiguration implements ContainerConfiguration
   {
      // A sentinel rather than null: "arquillian.xml said nothing" and
      // "arquillian.xml said nothing useful" have to read differently.
      private String name = "<unnamed>";
      // The protocol this adapter claims to speak. "Local" is the one the tree
      // itself registers, so the default is a run that works; a fixture naming
      // anything else is asking what happens when an adapter claims a protocol
      // nothing on the classpath implements.
      private String protocol = "Local";
      private String modeSetup = "";
      private String modeStart = "";
      private String modeStop = "";
      private String modeDeploy = "";
      private String modeUnDeploy = "";
      private String modeValidate = "";
      private int weight;
      private boolean chatty;

      public void setName(String v) { this.name = v; }
      public void setProtocol(String v) { this.protocol = v; }
      public void setModeSetup(String v) { this.modeSetup = v; }
      public void setModeStart(String v) { this.modeStart = v; }
      public void setModeStop(String v) { this.modeStop = v; }
      public void setModeDeploy(String v) { this.modeDeploy = v; }
      public void setModeUnDeploy(String v) { this.modeUnDeploy = v; }
      public void setModeValidate(String v) { this.modeValidate = v; }
      public void setWeight(int v) { this.weight = v; }
      public void setChatty(boolean v) { this.chatty = v; }

      public String getName() { return name; }

      public void validate() throws ConfigurationException
      {
         Log.add("config.validate:" + name);
         if (FAIL.equals(modeValidate))
         {
            throw new ConfigurationException("container " + name + " refused its own configuration");
         }
      }

      @Override
      public String toString()
      {
         return "StubConfiguration[name=" + name + " protocol=" + protocol
               + " weight=" + weight + " chatty=" + chatty
               + " modes=" + modeSetup + "/" + modeStart + "/" + modeStop
               + "/" + modeDeploy + "/" + modeUnDeploy + "/" + modeValidate + "]";
      }
   }

   /**
    * The registered container. Reads what to do from its configuration, logs
    * every call, and otherwise does nothing at all.
    */
   public static class Stub implements DeployableContainer<StubConfiguration>
   {
      /**
       * Proves the container was injected before setup.
       *
       * The lifecycle controller injects the deployable container before it
       * fires {@code BeforeSetup}; an integration that resolves a manager
       * instance in {@code setup} is relying on that, so it is recorded.
       */
      @Inject
      private Instance<ProtocolMetaData> injected;

      private StubConfiguration configuration;

      public Class<StubConfiguration> getConfigurationClass()
      {
         return StubConfiguration.class;
      }

      public void setup(StubConfiguration configuration)
      {
         this.configuration = configuration;
         // The whole configuration, not just its name: which declarations the
         // populator applied and which it silently dropped is most of what a
         // boot fixture is asking about, and it is visible nowhere else.
         Log.add("container.setup:" + configuration
               + " injected=" + (injected != null));
         if (FAIL.equals(configuration.modeSetup))
         {
            throw new IllegalStateException("setup refused by " + configuration.getName());
         }
      }

      public void start() throws LifecycleException
      {
         Log.add("container.start:" + name());
         if (mode("start"))
         {
            throw new LifecycleException("start refused by " + name());
         }
      }

      public void stop() throws LifecycleException
      {
         Log.add("container.stop:" + name());
         if (mode("stop"))
         {
            throw new LifecycleException("stop refused by " + name());
         }
      }

      public ProtocolDescription getDefaultProtocol()
      {
         String protocol = configuration == null ? "Local" : configuration.protocol;
         Log.add("container.getDefaultProtocol:" + name() + "->" + protocol);
         return new ProtocolDescription(protocol);
      }

      public ProtocolMetaData deploy(Archive<?> archive) throws DeploymentException
      {
         Log.add("container.deploy:" + name() + " archive=" + archive.getName());
         if (mode("deploy"))
         {
            throw new DeploymentException("deploy refused by " + name());
         }
         // A servlet context is what a real adapter reports back, and it is
         // what the servlet protocol's executor then looks for.
         return new ProtocolMetaData().addContext(
               new HTTPContext("localhost", 8080).add(new Servlet("ArquillianServletRunner", "/test")));
      }

      public void undeploy(Archive<?> archive) throws DeploymentException
      {
         Log.add("container.undeploy:" + name() + " archive=" + archive.getName());
         if (mode("undeploy"))
         {
            throw new DeploymentException("undeploy refused by " + name());
         }
      }

      public void deploy(Descriptor descriptor) throws DeploymentException
      {
         Log.add("container.deployDescriptor:" + name() + " descriptor=" + descriptor.getDescriptorName());
         if (mode("deploy"))
         {
            throw new DeploymentException("descriptor deploy refused by " + name());
         }
      }

      public void undeploy(Descriptor descriptor) throws DeploymentException
      {
         Log.add("container.undeployDescriptor:" + name() + " descriptor=" + descriptor.getDescriptorName());
         if (mode("undeploy"))
         {
            throw new DeploymentException("descriptor undeploy refused by " + name());
         }
      }

      private String name()
      {
         return configuration == null ? "<not-setup>" : configuration.getName();
      }

      private boolean mode(String phase)
      {
         if (configuration == null)
         {
            return false;
         }
         if ("start".equals(phase)) { return FAIL.equals(configuration.modeStart); }
         if ("stop".equals(phase)) { return FAIL.equals(configuration.modeStop); }
         if ("deploy".equals(phase)) { return FAIL.equals(configuration.modeDeploy); }
         if ("undeploy".equals(phase)) { return FAIL.equals(configuration.modeUnDeploy); }
         return false;
      }
   }
}
