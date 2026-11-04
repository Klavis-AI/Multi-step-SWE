package arqprobe.fam.servlet;

import java.util.ArrayList;
import java.util.List;

/**
 * What the protocol did to things it was handed, in order.
 *
 * The three seams this family drives all hide their work in a side effect. A
 * packager returns one archive but reaches into several, and which archive each
 * registered {@code ProtocolArchiveProcessor} was handed is the whole question —
 * on three of the four application shapes it is neither the archive the caller
 * passed nor the one that comes back, and nothing in the return value says which
 * it was. See {@code Stubs.Processor} for the measured table. The method
 * executor's observable is the request it sent, which only the server it sent it
 * to can see. The runner servlet writes to a response it is given rather than
 * returning anything at all.
 *
 * Per-thread because {@code Driver} runs each fixture on its own thread.
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
