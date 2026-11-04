package smoke;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.IntStream;

import static java.nio.file.Files.isDirectory;

/**
 * One file that moves every counter step 04's family C and B-10 read.
 *
 * <p>The report fixtures under fixtures/c/ prove the arithmetic over
 * SourceScan's numbers. They cannot prove the numbers, because they ARE the
 * numbers -- a hand-written report says whatever it was written to say. This
 * file is the other half: a real tree, compiled by the image's own javac with
 * the plugin attached, whose counts are known because they were measured and
 * then frozen into fixtures/src-rehearsal.sh.
 *
 * <p>The first half of the file is step 03's smoke file, unchanged. B-10 holds
 * this rung to the last rung's gains, so the counters it reads have to be
 * exercised here too -- and keeping the text identical means a drift in either
 * rung's reading of the same construct shows up as a disagreement between two
 * rehearsals rather than as two separately plausible numbers.
 *
 * <p>One counter does differ from step 03's reading, and it is not drift:
 * {@code loops()} in the second half declares {@code new ArrayList&lt;String&gt;()}
 * as the collecting loop's destination, so {@code newgeneric.explicit} is 3
 * here where step 03's smoke file reads 2. The rehearsal asserts 3 and says
 * why, which is the only way the difference stays a fact rather than becoming
 * a discrepancy somebody reconciles by editing an expectation.
 *
 * <pre>
 *   explicit   the two {@code new HashMap&lt;String, String&gt;()} forms, plus
 *              {@code loops()}'s destination list
 *   explicitanon  ...of which the second HashMap has a class body
 *   diamond    {@code new ArrayList&lt;&gt;()}
 *   raw        {@code new HashMap()}
 *   suppress   one site, two values
 *   try        two try statements, one of them with a resource clause
 *   close      one hand-written close() in a finally
 *   iofile     three java.io constructions, one java.io declaration
 *   niofile    one Path declaration, two member-select references, and one
 *              more reached through a static import, which is a bare
 *              identifier in the AST and which niofile.ref cannot see
 * </pre>
 *
 * <p>The second half is step 04's. Every construct is here for a named counter
 * AND for the way that counter can be got wrong; the rehearsal asserts each
 * count separately, so a counter that drifts is named by the failure instead of
 * being averaged away:
 *
 * <pre>
 *   iface.decl        three interfaces declared
 *   iface.sam         two of them have a single abstract method; Both does not
 *   iface.samannotated  one says so with @FunctionalInterface, which javac
 *                     enforces -- so this can never exceed iface.sam in a tree
 *                     whose source.ok is yes
 *   anonymousfunctional  three anonymous bodies over functional interfaces...
 *   convertible       ...of which one is convertible. The other two are the two
 *                     ways a one-method body still cannot be a lambda: one
 *                     declares a field, one names `this`.
 *   lambda            three, two with expression bodies
 *   methodref         two, one bound and one static
 *   foreach           three enhanced `for`s, one of which collects. The other
 *                     two are the two refusals: a two-statement body, and a
 *                     two-argument put that no stream form improves.
 *   loop              six loop statements in all: the three enhanced `for`s
 *                     above plus one indexed `for`, one `while` and one `do`.
 *                     C-3's no-reshape term reads this total, and the three
 *                     extra spellings are here because a census blind to any of
 *                     them would read identically on a tree that respelled its
 *                     collecting loops instead of migrating them.
 *   stream            two pipelines, entered once each, four stages between
 *                     them -- and three separate ways to miscount that:
 *                     {@code filter(..)} both takes and returns a stream, so a
 *                     counter calling every stream-returning call an entry
 *                     reports two entries in one pipeline;
 *                     {@code IntStream.range(..)} has a stream-typed receiver
 *                     that is a TYPE and not a value, so a counter reading the
 *                     receiver's type alone files an entry as a stage; and
 *                     {@code filter(String)} below is a method merely NAMED
 *                     filter, so a name-matching counter finds a stage in a
 *                     method that has no pipeline at all.
 * </pre>
 *
 * <p>It is also the only place C-4's two probes run against a real compiler:
 * this tree compiles at -source 1.8 and fails at -source 1.7, with every error
 * a language-level error. A tree that scored C-4 from the AST counters alone
 * would report the same thing without either compile having happened.
 */
public class Smoke {
   Map<String, String> explicit = new HashMap<String, String>();

   Map<String, String> anon = new HashMap<String, String>() {
      private static final long serialVersionUID = 1L;
   };

   List<String> dia = new ArrayList<>();

   @SuppressWarnings({ "rawtypes", "unchecked" })
   Map raw = new HashMap();

   public boolean io() throws Exception {
      File f = new File("x");
      InputStream in = new FileInputStream(f);
      try {
         in.read();
      } finally {
         in.close();
      }
      try (InputStream r = new FileInputStream("y")) {
         r.read();
      }
      Path p = Paths.get("z");
      return Files.exists(p) && !isDirectory(p);
   }

   // ------------------------------------------------------------ step 04

   @FunctionalInterface
   interface Handler
   {
      void handle(String s);
   }

   interface Mapper
   {
      String map(String s);
   }

   /** Two abstract methods, so not a SAM and not annotatable. */
   interface Both
   {
      String map(String s);

      void other();
   }

   void shout(String s)
   {
      if (s.isEmpty())
      {
         throw new IllegalArgumentException(s);
      }
   }

   /** A method merely NAMED filter, in a method with no pipeline in it. */
   String filter(String s)
   {
      return s;
   }

   Handler anonConvertible = new Handler()
   {
      public void handle(String s)
      {
         shout(s);
      }
   };

   Handler anonWithMembers = new Handler()
   {
      private int seen;

      public void handle(String s)
      {
         seen++;
      }
   };

   Mapper anonNamesSelf = new Mapper()
   {
      public String map(String s)
      {
         return this.toString() + s;
      }
   };

   Handler lambdaExpr = s -> shout(s);

   Handler lambdaBlock = s ->
   {
      shout(s);
      shout(s);
   };

   Mapper lambdaMap = s -> s.trim();

   Handler refBound = this::shout;

   Mapper refStatic = String::valueOf;

   public List<String> loops(List<String> src)
   {
      List<String> dst = new ArrayList<String>();
      for (String s : src)
      {
         dst.add(s.trim());
      }
      for (String s : src)
      {
         shout(s);
         shout(s);
      }
      for (String s : src)
      {
         raw.put(s, s);
      }
      return dst;
   }

   /**
    * The other three loop spellings, for the whole-tree census C-3 reads.
    *
    * <p>Nothing here collects and nothing here is an enhanced {@code for}, so
    * every counter but {@code loop.size} is blind to this method -- which is
    * the point. A `while` that walks the same collection moves
    * {@code foreach.collect} exactly as far as a pipeline does, and the total
    * is the only figure that knows the loop is still there.
    */
   public void spellings(List<String> src)
   {
      for (int i = 0; i < src.size(); i++)
      {
         shout(src.get(i));
      }
      int n = src.size();
      while (n > 0)
      {
         n--;
      }
      do
      {
         n++;
      }
      while (n < src.size());
   }

   public long counted(List<String> src)
   {
      return src.stream().filter(s -> !s.isEmpty()).count();
   }

   public long ranged()
   {
      return IntStream.range(0, 3).map(i -> i + 1).count();
   }
}
