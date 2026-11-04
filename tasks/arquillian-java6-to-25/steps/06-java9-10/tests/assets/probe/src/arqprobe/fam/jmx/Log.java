package arqprobe.fam.jmx;

import java.util.ArrayList;
import java.util.List;

/**
 * What the runner under test asked of the fixture's own pieces, in order.
 *
 * {@code JMXTestRunner} builds nothing the fixture hands it: the
 * {@code TestRunner} is instantiated by a {@code ServiceLoader} from a name in
 * a service file, so the fixture never holds the instance that ran and cannot
 * ask it anything afterwards. The stubs append here instead, and the fixture
 * reads the list.
 *
 * Per-thread, because {@code Driver} runs each fixture on its own thread.
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
