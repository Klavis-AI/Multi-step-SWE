package arqprobe.fam.runner;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * What ran, in order, inside a test framework's own run.
 *
 * A {@code TestRunner} returns one {@code TestResult} — a status and at most one
 * throwable — and that is a summary of a whole framework run. Everything the
 * integration actually decides is upstream of it: whether {@code @Before} ran
 * before the Arquillian {@code before} callback or after it, whether
 * {@code @After} still ran once the test body threw, whether the selected method
 * was the only one invoked, whether a {@code dependsOnMethods} was honoured or
 * stripped. None of that is in the return value, so the test bodies and
 * lifecycle methods append here and the fixture records the sequence next to the
 * result.
 *
 * <p><b>Not per-thread, unlike the servlet family's log.</b> TestNG owns the
 * threads its test methods run on and is free to use a worker rather than the
 * caller, so a {@code ThreadLocal} here would quietly record an empty sequence
 * for half this family — the same shape of mistake the servlet family made in
 * the other direction. A plain static list is safe because {@code Driver} starts
 * one fixture thread and joins it before starting the next, so only one fixture
 * is ever in flight; and within a fixture the caller is blocked inside
 * {@code TestNG.run()} while its workers append, so the order is still the order
 * things happened. The list is synchronized so a worker's writes are visible to
 * the thread that reads them back.
 */
public final class Log
{
   private static final List<String> LINES = Collections.synchronizedList(new ArrayList<String>());

   private Log()
   {
   }

   public static void reset()
   {
      LINES.clear();
   }

   public static void add(String s)
   {
      LINES.add(s);
   }

   public static List<String> lines()
   {
      synchronized (LINES)
      {
         return new ArrayList<String>(LINES);
      }
   }
}
