package arqprobe.fam.runner;

import java.util.ArrayList;
import java.util.List;

import org.jboss.arquillian.junit.Arquillian;
import org.jboss.arquillian.junit.JUnitTestRunner;
import org.jboss.arquillian.spi.ContainerProfile;
import org.junit.runner.Description;
import org.junit.runner.notification.Failure;
import org.junit.runner.notification.RunListener;

/**
 * The two documented extension points on {@code JUnitTestRunner}.
 *
 * Both are {@code protected} and one of them carries a javadoc saying what it is
 * for — <em>"Overwrite to provide additional run listeners."</em> — so a
 * subclass is the shipped way to reach them, not a back door. Nothing else in
 * this family reaches either: every other fixture constructs a plain
 * {@code JUnitTestRunner}, which returns an empty listener list and the
 * CONTAINER profile, so the loop that adds listeners runs zero times and the
 * profile argument never varies.
 *
 * <p>They are worth covering because they are the seam a container adapter uses.
 * A runner shipped into a container subclasses this to add its own reporting,
 * and a migration that changed when listeners are added relative to the
 * integration's own {@code ExpectedExceptionHolder} would change what those
 * listeners can still see — which is a real difference and an invisible one
 * unless a fixture looks.
 */
public final class Runners
{
   private Runners()
   {
   }

   /**
    * Adds one listener that writes what it was told into the shared log.
    *
    * {@code execute} registers its own {@code ExpectedExceptionHolder} first and
    * the overridden list afterwards, and the holder's {@code testFinished}
    * <em>consumes</em> {@code Arquillian.caughtTestException} — it reads the
    * {@code ThreadLocal} and immediately sets it back to null. JUnit notifies
    * listeners in registration order, so a later listener asking the same
    * question gets a different answer than an earlier one would. The listener
    * here asks it, so the transcript states the ordering rather than leaving it
    * to be inferred from two source files.
    */
   public static class Listening extends JUnitTestRunner
   {
      @Override
      protected List<RunListener> getRunListeners()
      {
         List<RunListener> listeners = new ArrayList<RunListener>();
         listeners.add(new RunListener()
         {
            @Override
            public void testStarted(Description description)
            {
               Log.add("listener:testStarted " + description.getMethodName());
            }

            @Override
            public void testFinished(Description description)
            {
               // Read, not cleared: whether anything is still here is the
               // question, and taking it would change the next fixture's answer.
               Throwable caught = Arquillian.caughtTestException.get();
               Log.add("listener:testFinished " + description.getMethodName()
                     + " caught=" + (caught == null ? "<null>" : caught.getClass().getName()));
            }

            @Override
            public void testFailure(Failure failure)
            {
               Log.add("listener:testFailure "
                     + failure.getException().getClass().getName());
            }

            @Override
            public void testIgnored(Description description)
            {
               Log.add("listener:testIgnored " + description.getMethodName());
            }
         });
         return listeners;
      }
   }

   /**
    * Runs on the CLIENT profile instead of the CONTAINER one.
    *
    * {@code execute} does not hard-code the profile, it calls
    * {@code getProfile()}, and the two profiles build genuinely different
    * managers: the container one registers seven extensions and no container
    * registry at all, the client one registers the deployment and container
    * machinery. What a run looks like on the other side of that switch is not
    * something the source says, and a migration that reversed the default or
    * stopped consulting the override would leave every other fixture in this
    * family passing.
    */
   public static class OnClientProfile extends JUnitTestRunner
   {
      @Override
      protected ContainerProfile getProfile()
      {
         return ContainerProfile.CLIENT;
      }
   }
}
