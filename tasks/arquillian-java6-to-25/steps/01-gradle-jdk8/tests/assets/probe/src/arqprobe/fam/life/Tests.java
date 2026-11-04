package arqprobe.fam.life;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URI;
import java.net.URL;

import org.jboss.arquillian.api.ArquillianResource;
import org.jboss.arquillian.api.Deployer;
import org.jboss.arquillian.api.Deployment;
import org.jboss.arquillian.api.OperateOnDeployment;
import org.jboss.arquillian.api.OverProtocol;
import org.jboss.arquillian.api.RunAsClient;
import org.jboss.arquillian.api.ShouldThrowException;
import org.jboss.arquillian.api.TargetsContainer;
import org.jboss.arquillian.impl.configuration.api.ArquillianDescriptor;
import org.jboss.arquillian.spi.LifecycleMethodExecutor;
import org.jboss.arquillian.spi.TestMethodExecutor;
import org.jboss.arquillian.spi.client.container.DeploymentException;
import org.jboss.shrinkwrap.api.ShrinkWrap;
import org.jboss.shrinkwrap.api.spec.JavaArchive;
import org.jboss.shrinkwrap.descriptor.api.Descriptor;
import org.jboss.shrinkwrap.descriptor.api.Descriptors;

import arqprobe.fam.scenario.Payload;

/**
 * The test classes a client run is asked to run, and the callbacks it runs them
 * through.
 *
 * A test framework integration hands Arquillian three things: a class, an
 * instance, and executors that know how to call a method on it. These are those
 * three things written by hand, so that a fixture can drive the adaptor without
 * a JUnit or TestNG runner in the way — the runners have their own family.
 *
 * Every class here declares at most one {@code @ArquillianResource} field.
 * {@code SecurityActions.getFieldsWithAnnotation} walks
 * {@code getDeclaredFields()}, whose order the JVM does not specify, so two
 * annotated fields would make the order they are injected in unrecordable.
 *
 * Method names are unique within each class for the same reason: {@link Exec}
 * finds a method by name and would otherwise have to pick between overloads
 * that {@code getMethods()} returns in no particular order.
 */
public final class Tests
{
   private Tests()
   {
   }

   public static JavaArchive jar(String name)
   {
      return ShrinkWrap.create(JavaArchive.class, name).addClass(Payload.class);
   }

   // --- callbacks the framework integration would supply --------------------

   /** A lifecycle callback that records that it was reached. */
   public static LifecycleMethodExecutor lifecycle(final String label)
   {
      return new LifecycleMethodExecutor()
      {
         public void invoke() throws Throwable
         {
            Log.add("lifecycle:" + label);
         }
      };
   }

   /** A lifecycle callback that records that it was reached and then refuses. */
   public static LifecycleMethodExecutor refusing(final String label)
   {
      return new LifecycleMethodExecutor()
      {
         public void invoke() throws Throwable
         {
            Log.add("lifecycle:" + label);
            throw new IllegalStateException(label + " refused");
         }
      };
   }

   /**
    * Calls one named method on one instance.
    *
    * The arguments are recorded because they are the whole of what enrichment
    * produced: a resolved {@code URL} parameter arrives here and nowhere else.
    */
   public static final class Exec implements TestMethodExecutor
   {
      private final Object instance;

      private final Method method;

      public Exec(Object instance, String methodName)
      {
         this.instance = instance;
         this.method = find(instance.getClass(), methodName);
      }

      public Method getMethod()
      {
         return method;
      }

      public Object getInstance()
      {
         return instance;
      }

      public void invoke(Object... parameters) throws Throwable
      {
         Log.add("test.invoke:" + method.getName() + " args=" + args(parameters));
         try
         {
            method.invoke(instance, parameters);
         }
         catch (InvocationTargetException e)
         {
            throw e.getCause();
         }
      }

      private static Method find(Class<?> type, String name)
      {
         Method found = null;
         for (Method m : type.getMethods())
         {
            if (!m.getName().equals(name))
            {
               continue;
            }
            if (found != null)
            {
               throw new IllegalStateException(
                     "more than one method named " + name + " on " + type.getName());
            }
            found = m;
         }
         if (found == null)
         {
            throw new IllegalStateException("no method named " + name + " on " + type.getName());
         }
         return found;
      }

      private static String args(Object[] parameters)
      {
         if (parameters == null)
         {
            return "<null>";
         }
         StringBuilder sb = new StringBuilder("[");
         for (int i = 0; i < parameters.length; i++)
         {
            if (i > 0)
            {
               sb.append(", ");
            }
            Object p = parameters[i];
            sb.append(p == null ? "<null>" : p.getClass().getName() + ":" + p);
         }
         return sb.append(']').toString();
      }
   }

   // --- test classes --------------------------------------------------------

   /** One testable archive, one passing method: the shape everything else varies. */
   public static class Plain
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:Plain.create");
         return jar("plain.jar");
      }

      public void passes()
      {
         Log.add("body:Plain.passes");
      }
   }

   /** The whole class runs in the client JVM. */
   @RunAsClient
   public static class ClientClass
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:ClientClass.create");
         return jar("client-class.jar");
      }

      public void passes()
      {
         Log.add("body:ClientClass.passes");
      }

      public void fails()
      {
         Log.add("body:ClientClass.fails");
         throw new IllegalStateException("the client-side test said no");
      }
   }

   /** One method runs in the client JVM and one does not. */
   public static class ClientMethod
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:ClientMethod.create");
         return jar("client-method.jar");
      }

      @RunAsClient
      public void here()
      {
         Log.add("body:ClientMethod.here");
      }

      public void there()
      {
         Log.add("body:ClientMethod.there");
      }
   }

   /** Nothing is packaged into the deployment, which forces client-side execution. */
   public static class NotTestable
   {
      @Deployment(testable = false)
      public static JavaArchive create()
      {
         Log.add("deployment:NotTestable.create");
         return jar("not-testable.jar");
      }

      public void passes()
      {
         Log.add("body:NotTestable.passes");
      }
   }

   /** The method throws, which is a result rather than an error. */
   public static class Failing
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:Failing.create");
         return jar("failing.jar");
      }

      public void fails()
      {
         Log.add("body:Failing.fails");
         throw new IllegalStateException("the test said no");
      }
   }

   /** Two deployments; one method names one and one names none. */
   public static class TwoDeployments
   {
      @Deployment(name = "one", order = 1)
      public static JavaArchive one()
      {
         Log.add("deployment:TwoDeployments.one");
         return jar("one.jar");
      }

      @Deployment(name = "two", order = 2)
      public static JavaArchive two()
      {
         Log.add("deployment:TwoDeployments.two");
         return jar("two.jar");
      }

      @OperateOnDeployment("two")
      public void onTwo()
      {
         Log.add("body:TwoDeployments.onTwo");
      }

      public void onDefault()
      {
         Log.add("body:TwoDeployments.onDefault");
      }
   }

   /** The deployment names a container by qualifier. */
   public static class Targeted
   {
      @Deployment
      @TargetsContainer("beta")
      public static JavaArchive create()
      {
         Log.add("deployment:Targeted.create");
         return jar("targeted.jar");
      }

      public void passes()
      {
         Log.add("body:Targeted.passes");
      }
   }

   /** The deployment names a container that no configuration defines. */
   public static class TargetsUnknown
   {
      @Deployment
      @TargetsContainer("nowhere")
      public static JavaArchive create()
      {
         Log.add("deployment:TargetsUnknown.create");
         return jar("targets-unknown.jar");
      }

      public void passes()
      {
         Log.add("body:TargetsUnknown.passes");
      }
   }

   /** The deployment names a protocol by description. */
   public static class OverLocal
   {
      @Deployment
      @OverProtocol("Local")
      public static JavaArchive create()
      {
         Log.add("deployment:OverLocal.create");
         return jar("over-local.jar");
      }

      public void passes()
      {
         Log.add("body:OverLocal.passes");
      }
   }

   /** The deployment names a protocol nothing implements. */
   public static class OverUnknown
   {
      @Deployment
      @OverProtocol("Telepathy")
      public static JavaArchive create()
      {
         Log.add("deployment:OverUnknown.create");
         return jar("over-unknown.jar");
      }

      public void passes()
      {
         Log.add("body:OverUnknown.passes");
      }
   }

   /** A descriptor rather than an archive: a different pair of container calls. */
   public static class DescriptorDeployment
   {
      @Deployment
      public static Descriptor create()
      {
         Log.add("deployment:DescriptorDeployment.create");
         return Descriptors.create(ArquillianDescriptor.class);
      }

      public void passes()
      {
         Log.add("body:DescriptorDeployment.passes");
      }
   }

   /** The deployment is expected to fail, and the run is asked about it. */
   public static class ShouldThrow
   {
      @Deployment
      @ShouldThrowException(DeploymentException.class)
      public static JavaArchive create()
      {
         Log.add("deployment:ShouldThrow.create");
         return jar("should-throw.jar");
      }

      public void passes()
      {
         Log.add("body:ShouldThrow.passes");
      }
   }

   /** The deployment method itself throws. */
   public static class ThrowingDeployment
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:ThrowingDeployment.create");
         throw new IllegalArgumentException("the deployment method said no");
      }

      public void passes()
      {
         Log.add("body:ThrowingDeployment.passes");
      }
   }

   /** No deployment at all: a test class the generator has nothing to say about. */
   public static class NoDeployment
   {
      public void passes()
      {
         Log.add("body:NoDeployment.passes");
      }
   }

   /** Asks for the deployment's URL in a field. */
   public static class WantsUrl
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:WantsUrl.create");
         return jar("wants-url.jar");
      }

      @ArquillianResource
      private URL url;

      public void passes()
      {
         Log.add("body:WantsUrl.passes url=" + url);
      }
   }

   /** Asks for the deployment's URI in a field. */
   public static class WantsUri
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:WantsUri.create");
         return jar("wants-uri.jar");
      }

      @ArquillianResource
      private URI uri;

      public void passes()
      {
         Log.add("body:WantsUri.passes uri=" + uri);
      }
   }

   /** Asks for the URL as a method argument instead of a field. */
   public static class WantsUrlArgument
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:WantsUrlArgument.create");
         return jar("wants-url-argument.jar");
      }

      public void passes(@ArquillianResource URL url)
      {
         Log.add("body:WantsUrlArgument.passes url=" + url);
      }
   }

   /**
    * Asks for a type nothing produces.
    *
    * {@code Deployer} has a provider but no producer behind it anywhere in the
    * client profile, so the provider resolves to null and the enricher reports
    * that rather than injecting it.
    */
   public static class WantsDeployer
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:WantsDeployer.create");
         return jar("wants-deployer.jar");
      }

      @ArquillianResource
      private Deployer deployer;

      public void passes()
      {
         Log.add("body:WantsDeployer.passes deployer=" + deployer);
      }
   }

   /** Asks for a type no provider claims at all. */
   public static class WantsUnclaimed
   {
      @Deployment
      public static JavaArchive create()
      {
         Log.add("deployment:WantsUnclaimed.create");
         return jar("wants-unclaimed.jar");
      }

      @ArquillianResource
      private StringBuilder unclaimed;

      public void passes()
      {
         Log.add("body:WantsUnclaimed.passes unclaimed=" + unclaimed);
      }
   }
}
