package arqprobe.fam.webrunner;

import java.util.ArrayList;
import java.util.List;

/**
 * What the servlet asked of the request and the response, in order.
 *
 * Order is the point. {@code ServletTestRunner.writeObject} calls
 * {@code setStatus(SC_OK)} <em>after</em> it has written the object into the
 * stream, which in a real container is a no-op because writing has already
 * committed the response. A fixture that only recorded the final status could
 * not tell that apart from a correct implementation; one that records the
 * sequence can.
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
