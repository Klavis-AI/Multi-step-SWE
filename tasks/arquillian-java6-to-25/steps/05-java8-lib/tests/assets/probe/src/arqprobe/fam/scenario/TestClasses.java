package arqprobe.fam.scenario;

import org.jboss.arquillian.api.Deployment;
import org.jboss.arquillian.api.OverProtocol;
import org.jboss.arquillian.api.ShouldThrowException;
import org.jboss.arquillian.api.TargetsContainer;
import org.jboss.arquillian.impl.configuration.api.ArquillianDescriptor;
import org.jboss.shrinkwrap.api.ShrinkWrap;
import org.jboss.shrinkwrap.api.spec.JavaArchive;
import org.jboss.shrinkwrap.api.spec.WebArchive;
import org.jboss.shrinkwrap.descriptor.api.Descriptor;
import org.jboss.shrinkwrap.descriptor.api.Descriptors;

/**
 * Test classes handed to the deployment scenario generator.
 *
 * Each nested class is one shape of {@code @Deployment} declaration — valid or
 * not — that the generator has to accept, reject, or describe.
 */
public final class TestClasses
{
   private TestClasses()
   {
   }

   public static JavaArchive jar(String name)
   {
      return ShrinkWrap.create(JavaArchive.class, name)
            .addClass(Payload.class);
   }

   public static ArquillianDescriptor descriptor()
   {
      return Descriptors.create(ArquillianDescriptor.class);
   }

   // --- accepted shapes -----------------------------------------------------

   public static class SingleArchive
   {
      @Deployment
      public static JavaArchive create()
      {
         return jar("single.jar");
      }
   }

   public static class SingleWebArchive
   {
      @Deployment
      public static WebArchive create()
      {
         return ShrinkWrap.create(WebArchive.class, "single.war");
      }
   }

   public static class SingleDescriptor
   {
      @Deployment
      public static Descriptor create()
      {
         return descriptor();
      }
   }

   /** Declared return type is the interface, not the concrete archive class. */
   public static class ArchiveInterfaceReturn
   {
      @Deployment
      public static org.jboss.shrinkwrap.api.Archive<?> create()
      {
         return jar("iface.jar");
      }
   }

   public static class NamedDeployment
   {
      @Deployment(name = "alpha")
      public static JavaArchive create()
      {
         return jar("alpha.jar");
      }
   }

   public static class NotTestable
   {
      @Deployment(testable = false)
      public static JavaArchive create()
      {
         return jar("nt.jar");
      }
   }

   public static class NotManaged
   {
      @Deployment(managed = false)
      public static JavaArchive create()
      {
         return jar("nm.jar");
      }
   }

   public static class ExplicitOrder
   {
      @Deployment(order = 7)
      public static JavaArchive create()
      {
         return jar("order.jar");
      }
   }

   public static class TargetedDeployment
   {
      @Deployment
      @TargetsContainer("container-x")
      public static JavaArchive create()
      {
         return jar("targeted.jar");
      }
   }

   public static class ProtocolDeployment
   {
      @Deployment
      @OverProtocol("Servlet 3.0")
      public static JavaArchive create()
      {
         return jar("protocol.jar");
      }
   }

   public static class ShouldThrow
   {
      @Deployment
      @ShouldThrowException(IllegalStateException.class)
      public static JavaArchive create()
      {
         return jar("throws.jar");
      }
   }

   /** {@code @ShouldThrowException} is documented to force testable off, even when asked for. */
   public static class ShouldThrowButTestable
   {
      @Deployment(testable = true)
      @ShouldThrowException(IllegalStateException.class)
      public static JavaArchive create()
      {
         return jar("throws-testable.jar");
      }
   }

   public static class AllAttributes
   {
      @Deployment(name = "everything", managed = false, order = 3, testable = false)
      @TargetsContainer("container-y")
      @OverProtocol("JMX")
      public static JavaArchive create()
      {
         return jar("everything.jar");
      }
   }

   public static class TwoDeployments
   {
      @Deployment(name = "one")
      public static JavaArchive one()
      {
         return jar("one.jar");
      }

      @Deployment(name = "two")
      public static JavaArchive two()
      {
         return jar("two.jar");
      }
   }

   public static class TwoOrderedDeployments
   {
      @Deployment(name = "second", order = 2)
      public static JavaArchive second()
      {
         return jar("second.jar");
      }

      @Deployment(name = "first", order = 1)
      public static JavaArchive first()
      {
         return jar("first.jar");
      }
   }

   public static class MixedManagedDeployments
   {
      @Deployment(name = "managed-a", order = 1)
      public static JavaArchive a()
      {
         return jar("a.jar");
      }

      @Deployment(name = "unmanaged-b", managed = false, order = 2)
      public static JavaArchive b()
      {
         return jar("b.jar");
      }

      @Deployment(name = "managed-c", order = 3)
      public static JavaArchive c()
      {
         return jar("c.jar");
      }
   }

   public static class TwoTargets
   {
      @Deployment(name = "on-x")
      @TargetsContainer("x")
      public static JavaArchive x()
      {
         return jar("x.jar");
      }

      @Deployment(name = "on-y")
      @TargetsContainer("y")
      public static JavaArchive y()
      {
         return jar("y.jar");
      }
   }

   public static class TwoProtocols
   {
      @Deployment(name = "via-servlet")
      @OverProtocol("Servlet 3.0")
      public static JavaArchive servlet()
      {
         return jar("servlet.jar");
      }

      @Deployment(name = "via-jmx")
      @OverProtocol("JMX")
      public static JavaArchive jmx()
      {
         return jar("jmx.jar");
      }
   }

   public static class InheritedDeployment extends SingleArchive
   {
   }

   public static class DuplicateNames
   {
      @Deployment(name = "same")
      public static JavaArchive one()
      {
         return jar("same-1.jar");
      }

      @Deployment(name = "same")
      public static JavaArchive two()
      {
         return jar("same-2.jar");
      }
   }

   // --- rejected shapes -----------------------------------------------------

   public static class NoDeploymentMethod
   {
      public static JavaArchive create()
      {
         return jar("none.jar");
      }
   }

   public static class NonStaticDeployment
   {
      @Deployment
      public JavaArchive create()
      {
         return jar("nonstatic.jar");
      }
   }

   public static class WrongReturnType
   {
      @Deployment
      public static String create()
      {
         return "not-an-archive";
      }
   }

   public static class VoidReturnType
   {
      @Deployment
      public static void create()
      {
      }
   }

   public static class WithParameters
   {
      @Deployment
      public static JavaArchive create(String unexpected)
      {
         return jar("params.jar");
      }
   }

   /** Not public, so {@code Class.getMethods()} never sees it. */
   public static class PrivateStaticDeployment
   {
      @Deployment
      static JavaArchive create()
      {
         return jar("private.jar");
      }
   }

   public static class ThrowingDeployment
   {
      @Deployment
      public static JavaArchive create()
      {
         throw new IllegalStateException("deployment method refused");
      }
   }

   public static class NullReturningDeployment
   {
      @Deployment
      public static JavaArchive create()
      {
         return null;
      }
   }

   public static class OneValidOneInvalid
   {
      @Deployment(name = "valid")
      public static JavaArchive valid()
      {
         return jar("valid.jar");
      }

      @Deployment(name = "invalid")
      public JavaArchive invalid()
      {
         return jar("invalid.jar");
      }
   }
}
