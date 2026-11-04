package arqprobe.fam.enrich;

import java.util.ArrayList;
import java.util.List;

/**
 * What the enrichers asked of the environment, in order.
 *
 * An enricher returns nothing. Everything it does is done to objects it was
 * handed — a JNDI context, a bean manager, the test instance — so the only way
 * to see it work is to hand it instrumented ones and read back what they were
 * asked. That sequence is the observable, and its order is part of it:
 * {@code EJBInjectionEnricher} tries eleven JNDI names in a fixed order, and a
 * migration that reordered them would be a behaviour change no return value
 * would show.
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
