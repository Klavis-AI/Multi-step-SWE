package arqprobe.fam.core;

import java.util.ArrayList;
import java.util.List;

/**
 * Ordered log of what the manager called, in the order it called it.
 *
 * Observers cannot hand a value back to the fixture — the manager owns their
 * lifecycle — so they append here instead and the fixture reads the log
 * afterwards. Cleared at the start of every fixture.
 */
public final class Trace
{
   private static final List<String> EVENTS = new ArrayList<String>();

   private static int depth;

   private Trace()
   {
   }

   public static void reset()
   {
      EVENTS.clear();
      depth = 0;
   }

   public static void add(String s)
   {
      EVENTS.add(s);
   }

   public static List<String> events()
   {
      return new ArrayList<String>(EVENTS);
   }

   /** Bounded re-entry counter, so a self-firing observer records a depth instead of a stack overflow. */
   public static boolean enter(int max)
   {
      return depth++ < max;
   }

   public static int depth()
   {
      return depth;
   }
}
