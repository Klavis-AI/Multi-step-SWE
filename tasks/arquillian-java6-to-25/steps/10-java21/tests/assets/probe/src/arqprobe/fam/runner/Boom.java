package arqprobe.fam.runner;

/**
 * The exception a fixture's test body throws, owned by the fixture.
 *
 * Deliberately not an assertion failure. An {@code AssertionError} from
 * {@code Assert.assertEquals} carries a message the test framework composed
 * ({@code expected:<1> but was:<2>}), which is the framework's sentence about
 * its own machinery and is exactly the kind of text a version bump is allowed to
 * reword — the same trap as the JDK-composed messages in {@code Rec}. A
 * throwable defined here has a type and a message this suite owns, so it can be
 * recorded in full and will still read the same after the test stack moves.
 *
 * It extends {@code RuntimeException} so it needs no {@code throws} clause, and
 * carries the two constructors the Arquillian {@code ExceptionProxy} looks for,
 * so a fixture that sends one through the wire family's machinery gets it back
 * as itself rather than as a substitute.
 */
public class Boom extends RuntimeException
{
   private static final long serialVersionUID = 1L;

   public Boom(String message)
   {
      super(message);
   }

   public Boom(String message, Throwable cause)
   {
      super(message, cause);
   }

   /**
    * A throwable that is not an {@code Exception}.
    *
    * {@code Arquillian.multiExecute} catches {@code Exception}, not
    * {@code Throwable}, and the JUnit integration routes {@code @After} through
    * it — so whether an {@code Error} from a test body lets the teardown run is
    * a different question from whether an exception does. This is how a fixture
    * asks the second one.
    */
   public static class Hard extends Error
   {
      private static final long serialVersionUID = 1L;

      public Hard(String message)
      {
         super(message);
      }
   }
}
