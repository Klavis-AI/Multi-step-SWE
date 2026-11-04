package arqprobe.fam.domain;

import java.io.File;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;

import org.jboss.arquillian.spi.ConfigurationException;
import org.jboss.arquillian.spi.ContainerMethodExecutor;
import org.jboss.arquillian.spi.ServiceLoader;
import org.jboss.arquillian.spi.client.container.ContainerConfiguration;
import org.jboss.arquillian.spi.client.container.DeployableContainer;
import org.jboss.arquillian.spi.client.container.DeploymentException;
import org.jboss.arquillian.spi.client.container.LifecycleException;
import org.jboss.arquillian.spi.client.deployment.DeploymentPackager;
import org.jboss.arquillian.spi.client.protocol.Protocol;
import org.jboss.arquillian.spi.client.protocol.ProtocolConfiguration;
import org.jboss.arquillian.spi.client.protocol.ProtocolDescription;
import org.jboss.arquillian.spi.client.protocol.metadata.ProtocolMetaData;
import org.jboss.shrinkwrap.api.Archive;
import org.jboss.shrinkwrap.descriptor.api.Descriptor;

/**
 * The pluggable pieces the container domain expects an integration to supply.
 *
 * The domain model here is entirely about what it does with somebody else's
 * container and somebody else's protocol, so a fixture has to bring one of each.
 * These are the smallest implementations that let the registries be driven, plus
 * a handful of deliberately awkward configuration classes: the domain reaches a
 * configuration class through {@code Class.newInstance()}, and what comes out
 * when that call cannot succeed is an observable the migration moves.
 */
public final class Stubs
{
   private Stubs()
   {
   }

   /** A class loaded to find out which loader found it. Nothing else uses it. */
   public static final class Marker
   {
      public static final String NAME = Marker.class.getName();
   }

   // ---------------------------------------------------------------- configs

   /** Every type {@code MapObject} knows how to convert, and one it does not. */
   public static class Config implements ContainerConfiguration
   {
      private int anInt;
      private Integer anInteger;
      private long aLong;
      private Long aLongObject;
      private double aDouble;
      private Double aDoubleObject;
      private boolean aBoolean;
      private Boolean aBooleanObject;
      // Sentinels, not nulls: a property the populator set to null and one it
      // never reached would otherwise read the same in a transcript.
      private String aString = "<unset>";
      private File aFile;
      private String readOnly = "untouched";

      public void setAnInt(int v) { this.anInt = v; }
      public void setAnInteger(Integer v) { this.anInteger = v; }
      public void setALong(long v) { this.aLong = v; }
      public void setALongObject(Long v) { this.aLongObject = v; }
      public void setADouble(double v) { this.aDouble = v; }
      public void setADoubleObject(Double v) { this.aDoubleObject = v; }
      public void setABoolean(boolean v) { this.aBoolean = v; }
      public void setABooleanObject(Boolean v) { this.aBooleanObject = v; }
      public void setAString(String v) { this.aString = v; }
      public void setAFile(File v) { this.aFile = v; }

      /** Not void, so the populator's shape test rejects it. */
      public Config setChained(String v) { this.readOnly = v; return this; }

      /** Two arguments, so the populator's shape test rejects it. */
      public void setPair(String a, String b) { this.readOnly = a + b; }

      /** Lower case after {@code set}, so the populator's name test rejects it. */
      public void setlowercase(String v) { this.readOnly = v; }

      public void validate() throws ConfigurationException
      {
      }

      @Override
      public String toString()
      {
         return "anInt=" + anInt + " anInteger=" + anInteger
               + " aLong=" + aLong + " aLongObject=" + aLongObject
               + " aDouble=" + aDouble + " aDoubleObject=" + aDoubleObject
               + " aBoolean=" + aBoolean + " aBooleanObject=" + aBooleanObject
               + " aString=" + aString + " aFile=" + aFile
               + " readOnly=" + readOnly;
      }
   }

   /** Refuses itself, so {@code validate()}'s failure has somewhere to come from. */
   public static class InvalidConfig implements ContainerConfiguration
   {
      public void validate() throws ConfigurationException
      {
         throw new ConfigurationException("configuration refused itself");
      }
   }

   /** No public no-argument constructor: what {@code newInstance()} does about that is the observable. */
   public static class NoPublicCtorConfig implements ContainerConfiguration
   {
      NoPublicCtorConfig()
      {
      }

      public void validate() throws ConfigurationException
      {
      }
   }

   /**
    * A public constructor, but not a no-argument one.
    *
    * {@code Class.newInstance()} reports this as an
    * {@code InstantiationException} with a {@code NoSuchMethodException} beneath
    * it; the replacement reports the {@code NoSuchMethodException} directly. One
    * of the three shapes where the two calls genuinely disagree.
    */
   public static class NoNoArgCtorConfig implements ContainerConfiguration
   {
      public NoNoArgCtorConfig(int unused)
      {
      }

      public void validate() throws ConfigurationException
      {
      }
   }

   /** Constructor fails: whether the failure arrives wrapped is the observable. */
   public static class ThrowingCtorConfig implements ContainerConfiguration
   {
      public ThrowingCtorConfig()
      {
         throw new IllegalStateException("constructor refused");
      }

      public void validate() throws ConfigurationException
      {
      }
   }

   /** Constructor fails with a checked exception the interface does not declare. */
   public static class CheckedCtorConfig implements ContainerConfiguration
   {
      public CheckedCtorConfig() throws Exception
      {
         throw new Exception("constructor refused, checked");
      }

      public void validate() throws ConfigurationException
      {
      }
   }

   /** Cannot be instantiated at all. */
   public abstract static class AbstractConfig implements ContainerConfiguration
   {
      public void validate() throws ConfigurationException
      {
      }
   }

   /**
    * Package-private class with public setters: the populator calls
    * {@code setAccessible(true)} before invoking, and that is the case the call
    * is load-bearing for.
    */
   static class HiddenConfig implements ContainerConfiguration
   {
      private String aString;

      public HiddenConfig()
      {
      }

      public void setAString(String v) { this.aString = v; }

      public void validate() throws ConfigurationException
      {
      }

      @Override
      public String toString()
      {
         return "aString=" + aString;
      }
   }

   /** A setter that fails, so the populator's own error handling is reachable. */
   public static class ThrowingSetterConfig implements ContainerConfiguration
   {
      public void setBoom(String v)
      {
         throw new UnsupportedOperationException("setter refused: " + v);
      }

      public void validate() throws ConfigurationException
      {
      }
   }

   public static ContainerConfiguration hiddenConfig()
   {
      return new HiddenConfig();
   }

   public static Class<? extends ContainerConfiguration> hiddenConfigClass()
   {
      return HiddenConfig.class;
   }

   // ------------------------------------------------------------- containers

   /** A container that does nothing but name its configuration class. */
   public static class TestContainer implements DeployableContainer<ContainerConfiguration>
   {
      private final Class<? extends ContainerConfiguration> configClass;
      private final ProtocolDescription defaultProtocol;

      public TestContainer()
      {
         this(Config.class, new ProtocolDescription("stub-protocol"));
      }

      public TestContainer(Class<? extends ContainerConfiguration> configClass,
            ProtocolDescription defaultProtocol)
      {
         this.configClass = configClass;
         this.defaultProtocol = defaultProtocol;
      }

      @SuppressWarnings("unchecked")
      public Class<ContainerConfiguration> getConfigurationClass()
      {
         return (Class<ContainerConfiguration>) (Class<?>) configClass;
      }

      public void setup(ContainerConfiguration configuration) { }
      public void start() throws LifecycleException { }
      public void stop() throws LifecycleException { }
      public ProtocolDescription getDefaultProtocol() { return defaultProtocol; }
      public ProtocolMetaData deploy(Archive<?> archive) throws DeploymentException { return null; }
      public void undeploy(Archive<?> archive) throws DeploymentException { }
      public void deploy(Descriptor descriptor) throws DeploymentException { }
      public void undeploy(Descriptor descriptor) throws DeploymentException { }

      @Override
      public String toString()
      {
         return "TestContainer(" + configClass.getSimpleName() + ")";
      }
   }

   // -------------------------------------------------------------- protocols

   public static class ProtoConfig implements ProtocolConfiguration
   {
      private String host = "<unset>";
      private int port;

      public void setHost(String v) { this.host = v; }
      public void setPort(int v) { this.port = v; }

      @Override
      public String toString()
      {
         return "host=" + host + " port=" + port;
      }
   }

   /** No public no-argument constructor, for the same reason as the container side. */
   public static class NoPublicCtorProtoConfig implements ProtocolConfiguration
   {
      NoPublicCtorProtoConfig()
      {
      }
   }

   public static class TestProtocol implements Protocol<ProtocolConfiguration>
   {
      private final ProtocolDescription description;
      private final Class<? extends ProtocolConfiguration> configClass;

      public TestProtocol(String name)
      {
         this(name, ProtoConfig.class);
      }

      public TestProtocol(String name, Class<? extends ProtocolConfiguration> configClass)
      {
         this.description = new ProtocolDescription(name);
         this.configClass = configClass;
      }

      @SuppressWarnings("unchecked")
      public Class<ProtocolConfiguration> getProtocolConfigurationClass()
      {
         return (Class<ProtocolConfiguration>) (Class<?>) configClass;
      }

      public ProtocolDescription getDescription() { return description; }
      public DeploymentPackager getPackager() { return null; }

      public ContainerMethodExecutor getExecutor(ProtocolConfiguration protocolConfiguration,
            ProtocolMetaData metaData)
      {
         return null;
      }

      @Override
      public String toString()
      {
         return "TestProtocol(" + description.getName() + ")";
      }
   }

   // ----------------------------------------------------------- service load

   /**
    * A service loader that returns exactly what it was handed.
    *
    * The registry under test asks a loader for a container implementation and
    * turns whatever it gets — including nothing, and including too many — into
    * its own outcome. Handing it a fixed answer is how that outcome is driven
    * without also driving the real discovery machinery, which has its own family.
    */
   public static class FixedServiceLoader implements ServiceLoader
   {
      private final List<Object> services;

      public FixedServiceLoader(Object... services)
      {
         this.services = new ArrayList<Object>();
         Collections.addAll(this.services, services);
      }

      @SuppressWarnings("unchecked")
      public <T> Collection<T> all(Class<T> serviceClass)
      {
         List<T> out = new ArrayList<T>();
         for (Object s : services)
         {
            if (serviceClass.isInstance(s))
            {
               out.add((T) s);
            }
         }
         return out;
      }

      public <T> Collection<T> all(ClassLoader classLoader, Class<T> serviceClass)
      {
         return all(serviceClass);
      }

      public <T> T onlyOne(Class<T> serviceClass)
      {
         Collection<T> found = all(serviceClass);
         if (found.size() != 1)
         {
            throw new IllegalStateException(
                  "Expected one service of type " + serviceClass.getName() + ", found " + found.size());
         }
         return found.iterator().next();
      }

      public <T> T onlyOne(ClassLoader classLoader, Class<T> serviceClass)
      {
         return onlyOne(serviceClass);
      }

      public <T> T onlyOne(Class<T> serviceClass, Class<? extends T> defaultServiceClass)
      {
         Collection<T> found = all(serviceClass);
         if (found.isEmpty())
         {
            try
            {
               return defaultServiceClass.getConstructor().newInstance();
            }
            catch (Exception e)
            {
               throw new IllegalStateException("Could not build default " + defaultServiceClass, e);
            }
         }
         return onlyOne(serviceClass);
      }

      public <T> T onlyOne(ClassLoader classLoader, Class<T> serviceClass,
            Class<? extends T> defaultServiceClass)
      {
         return onlyOne(serviceClass, defaultServiceClass);
      }
   }
}
