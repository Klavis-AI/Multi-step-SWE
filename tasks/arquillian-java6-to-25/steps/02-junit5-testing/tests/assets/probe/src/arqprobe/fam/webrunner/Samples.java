package arqprobe.fam.webrunner;

/**
 * The classes the servlet is asked to load, and the classes
 * {@code SecurityActions.newInstance} is asked to build.
 *
 * They are deliberately dull. Nothing here is a test in any framework's sense:
 * {@code ServletTestRunner} never looks at the class it loads, it hands the
 * {@code Class} object straight to whatever {@code TestRunner} the service
 * lookup produced, so the only thing a "test class" has to be is loadable by
 * name.
 */
public final class Samples
{
   private Samples()
   {
   }

   /** The ordinary test class: loadable, with a method the runner is told about. */
   public static class Sample
   {
      public void ok()
      {
      }
   }

   /** A test class whose binary name carries two {@code $} separators. */
   public static class Nested
   {
      public static class Inner
      {
         public void ok()
         {
         }
      }
   }

   // ------------------------------------------------------------------------
   // newInstance targets
   // ------------------------------------------------------------------------

   /** A no-argument public constructor: the case newInstance is written for. */
   public static class Plain
   {
      public Plain()
      {
      }

      @Override
      public String toString()
      {
         return "Plain";
      }
   }

   /** Two arguments, so the argumentTypes array has to match to find it. */
   public static class Pair
   {
      private final String left;
      private final int right;

      public Pair(String left, int right)
      {
         this.left = left;
         this.right = right;
      }

      @Override
      public String toString()
      {
         return "Pair(" + left + "," + right + ")";
      }
   }

   /** A constructor that throws: the failure arrives wrapped, twice over. */
   public static class Explodes
   {
      public Explodes()
      {
         throw new IllegalStateException("this one never builds");
      }
   }

   /** Public class, non-public constructor: getConstructor cannot see it at all. */
   public static class Hidden
   {
      Hidden()
      {
      }
   }

   /** Not a {@link Plain}, so the expectedType cast at the end of newInstance fails. */
   public static class Unrelated
   {
      public Unrelated()
      {
      }

      @Override
      public String toString()
      {
         return "Unrelated";
      }
   }
}

/**
 * Package-private, with a public constructor. {@code getConstructor} finds the
 * constructor and {@code newInstance} then fails on access, because the class
 * itself is not reachable from the caller's package -- a different failure from
 * {@link Samples.Hidden}, and one that lands in the same catch block.
 */
class PackagePrivate
{
   public PackagePrivate()
   {
   }

   @Override
   public String toString()
   {
      return "PackagePrivate";
   }
}
