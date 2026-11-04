package arqprobe.fam.wire;

/**
 * Exception shapes for the {@code wire} family.
 *
 * {@code ExceptionProxy} rebuilds a throwable by searching for a
 * {@code (String,Throwable)}, {@code (String,Exception)} or {@code (String)}
 * constructor, in that order. One class per outcome.
 */
public final class Exceptions
{
   private Exceptions()
   {
   }

   /** Frames are synthetic so the transcript does not depend on the probe's own line numbers. */
   public static <T extends Throwable> T frozen(T t)
   {
      t.setStackTrace(new StackTraceElement[]
      {
         new StackTraceElement("arqprobe.Remote", "invoke", "Remote.java", 41),
         new StackTraceElement("arqprobe.Remote", "call", "Remote.java", 17),
      });
      return t;
   }

   public static class StringThrowableCtor extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      public StringThrowableCtor(String message, Throwable cause)
      {
         super(message, cause);
      }
   }

   /** Only the narrower {@code (String,Exception)} signature, so the first probe misses. */
   public static class StringExceptionCtor extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      public StringExceptionCtor(String message, Exception cause)
      {
         super(message, cause);
      }
   }

   public static class StringOnlyCtor extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      public StringOnlyCtor(String message)
      {
         super(message);
      }
   }

   /** No constructor the proxy knows how to call. */
   public static class NoArgCtorOnly extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      public NoArgCtorOnly()
      {
         super("built by the no-arg constructor");
      }
   }

   public static class ThrowingCtor extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      public ThrowingCtor(String message)
      {
         super(message);
         throw new IllegalStateException("constructor refused: " + message);
      }
   }

   /** {@code getConstructor} only sees public constructors. */
   public static class PrivateStringCtor extends RuntimeException
   {
      private static final long serialVersionUID = 1L;

      private PrivateStringCtor(String message)
      {
         super(message);
      }

      public static PrivateStringCtor of(String message)
      {
         return new PrivateStringCtor(message);
      }
   }

   public static class CheckedFailure extends Exception
   {
      private static final long serialVersionUID = 1L;

      public CheckedFailure(String message, Throwable cause)
      {
         super(message, cause);
      }
   }

   public static class FailureError extends Error
   {
      private static final long serialVersionUID = 1L;

      public FailureError(String message, Throwable cause)
      {
         super(message, cause);
      }
   }
}
