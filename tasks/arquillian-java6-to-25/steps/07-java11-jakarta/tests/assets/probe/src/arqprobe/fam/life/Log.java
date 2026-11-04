package arqprobe.fam.life;

import java.util.ArrayList;
import java.util.List;

/**
 * What the manager called on the fixture's own pluggable pieces, in order.
 *
 * The client run is driven through {@code TestRunnerAdaptor}, which returns
 * almost nothing: the interesting part is the sequence of calls the manager
 * makes on the container, the protocol and the test class on the way through.
 * Those objects are built by the manager, not by the fixture, so they cannot
 * hand anything back directly and append here instead.
 *
 * Per-thread because {@code Driver} runs each fixture on its own thread, and
 * because the manager hands work to the same objects from several of its own
 * phases.
 */
public final class Log
{
   private static final ThreadLocal<List<String>> LINES = new ThreadLocal<List<String>>();

   private Log()
   {
   }

   public static void reset()
   {
      LINES.set(new ArrayList<String>());
   }

   public static void add(String s)
   {
      lines0().add(s);
   }

   public static List<String> lines()
   {
      return new ArrayList<String>(lines0());
   }

   private static List<String> lines0()
   {
      List<String> lines = LINES.get();
      if (lines == null)
      {
         lines = new ArrayList<String>();
         LINES.set(lines);
      }
      return lines;
   }
}
