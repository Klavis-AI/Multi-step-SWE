package smoke;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.lang.reflect.AccessibleObject;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.IntStream;

import static java.nio.file.Files.isDirectory;
import static java.util.stream.Collectors.toList;

/**
 * One file that moves every counter step 05's family C, B-10 and B-11 read.
 *
 * <p>The report fixtures under fixtures/c/ prove the arithmetic over
 * SourceScan's numbers. They cannot prove the numbers, because they ARE the
 * numbers -- a hand-written report says whatever it was written to say. This
 * file is the other half: a real tree, compiled by the image's own javac with
 * the plugin attached, whose counts are known because they were measured and
 * then frozen into fixtures/src-rehearsal.sh.
 *
 * <p>The first half of the file is step 03's smoke file, unchanged. B-10 holds
 * this rung to step 03's gains, so the counters it reads have to be exercised
 * here too -- and keeping the text identical means a drift in either rung's
 * reading of the same construct shows up as a disagreement between two
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
 * <p>The second half is step 04's, also unchanged, and B-11 is the gate that
 * holds it. Every construct is here for a named counter AND for the way that
 * counter can be got wrong; the rehearsal asserts each count separately, so a
 * counter that drifts is named by the failure instead of being averaged away:
 *
 * <pre>
 *   iface.decl        three interfaces declared... and step 05 adds a fourth
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
 *   stream            two pipelines, entered once each, four stages between
 *                     them -- and three separate ways to miscount that, which
 *                     the step-04 rehearsal sets out in full.
 * </pre>
 *
 * <p>The third half is step 05's: the library. Five findings, one construct
 * apiece and at least one near miss apiece, because every one of these counters
 * resolves through javac's ELEMENTS and the whole point of doing so is that a
 * name-matching counter answers differently. The near misses are the assertions
 * that would pass on a name-matching counter and fail on this one, and they are
 * listed with their counters in fixtures/src-rehearsal.sh.
 *
 * <pre>
 *   retnull    eight `return null;` statements in this file, across four
 *              modifier states, three scopes and both answers to api=
 *   optret     two Optional-returning declarations, and thirteen optop calls
 *              split four make / nine use, exactly one of which is get()
 *   accum      five `forEach(sink::add)` sites in all three operations the
 *              counter accepts, and the two shapes it refuses
 *   collect    five `collect(Collectors..)` calls in four factory spellings,
 *              one of them reached through a static import
 *   objects    equals/hash in the two members C-3 buckets on, and a
 *              requireNonNull in a method it buckets away from
 *   jtime      four java.time calls; millis three legacy ones in three of the
 *              seven shapes the counter takes
 * </pre>
 *
 * <p>Lib.java, beside this file, is the rest of it: the impostors -- a type of
 * this tree's own called {@code Optional}, another called {@code Objects}, a
 * collector factory that is not {@code Collectors} -- and the second file that
 * makes every {@code .files} counter something other than 1. A one-file smoke
 * tree cannot tell {@code distinctFiles} from {@code size}.
 *
 * <p>It is also where C-4's two probes run against a real compiler: this tree
 * compiles at -source 1.8 and fails at -source 1.7, with every error a language
 * level error. At this step the 1.7 result is a published fact rather than a
 * gate -- B-4 asks the compiled form of the same question -- but the probe is
 * still the shipped one, so the rehearsal still runs it.
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

   // ------------------------------------------------------------ step 05
   //
   // The library half. Every construct below exists for one of family C's five
   // findings and for the specific way that finding can be got wrong; the near
   // misses are as deliberate as the hits, and each one is named in
   // fixtures/src-rehearsal.sh beside the counter it would fool.

   /**
    * A fourth interface, and the retnull record whose {@code mods} and
    * {@code api} fields disagree.
    *
    * <p>{@code byDefault} carries no {@code public} keyword, so {@code modsOf}
    * reads {@code package}; {@code apisig.sh} prints it anyway, because every
    * member of an interface is implicitly public. A gate deciding api=yes off
    * the modifier set alone would file this one as private business and invite
    * a candidate to move a published signature for free. It is also not
    * abstract, so {@code iface.abstract} must not count it.
    */
   interface Lookup
   {
      String byName(String k);

      default String byDefault(String k)
      {
         if (!k.isEmpty())
         {
            return k;
         }
         return null;
      }
   }

   private final Map<String, String> table = new HashMap<>();

   /** api=no, mods=private: the population C-1 is allowed to convert. */
   private String lookupPrivate(String k)
   {
      if (table.containsKey(k))
      {
         return table.get(k);
      }
      return null;
   }

   /** api=yes, mods=public: B-6 charges for moving this one, so C-1 leaves it. */
   public String lookupPublic(String k)
   {
      if (table.containsKey(k))
      {
         return table.get(k);
      }
      return null;
   }

   /** api=yes, mods=protected: javap -protected prints these too. */
   protected String lookupProtected(String k)
   {
      return null;
   }

   /** api=no, mods=package: the fourth modifier state, and not private. */
   String lookupPackage(String k)
   {
      return null;
   }

   /**
    * A null that is not a {@code return null}, and so not a retnull site: the
    * returned expression is a conditional, not the null literal. A grep for
    * `return.*null` finds it; visitReturn does not.
    */
   String ternaryNull(String k)
   {
      return k.isEmpty() ? null : k;
   }

   /**
    * scope=anon, in=- : an override inside a field initializer belongs to no
    * method, because enclosingMethod() resets at an anonymous class body and
    * there is no named method outside this one.
    */
   Mapper anonReturnsNull = new Mapper()
   {
      public String map(String s)
      {
         if (s.isEmpty())
         {
            return null;
         }
         return s;
      }
   };

   /**
    * scope=anon, in=anonInMethod : the same override inside a method is
    * attributed to the OUTERMOST enclosing named method and not to {@code map}.
    * Both records are asserted, because the two halves of that rule are what
    * makes a bucket a bucket.
    */
   public Mapper anonInMethod()
   {
      return new Mapper()
      {
         public String map(String s)
         {
            if (s.isEmpty())
            {
               return null;
            }
            return s;
         }
      };
   }

   /**
    * scope=lambda, mods=- ret=- : a lambda body returns to a target type, so
    * there is no declaration whose signature a candidate could move and no
    * modifiers to print.
    */
   public Mapper lambdaReturnsNull()
   {
      return s ->
      {
         if (s.isEmpty())
         {
            return null;
         }
         return s;
      };
   }

   /** C-1's arrival half, in the spelling the import gives. */
   private Optional<String> findPrivate(String k)
   {
      return Optional.ofNullable(table.get(k));
   }

   /**
    * And with a nested type argument. {@code name()} erases before it compares,
    * so the argument does not have to be guessed at -- but the record prints
    * the return type AS WRITTEN, because that is what a reader of a failing
    * gate needs to see.
    */
   public Optional<Map<String, String>> findNested()
   {
      return Optional.of(explicit);
   }

   /**
    * The consumption spellings C-1's third term accepts, all but one of them.
    * Chained: {@code map} returns an Optional, so {@code filter} and
    * {@code orElseGet} are calls ON an Optional as much as the first one is.
    */
   public String uses(String k)
   {
      Optional<String> a = Optional.ofNullable(lookupPrivate(k));
      Optional<String> e = Optional.empty();
      if (a.isPresent())
      {
         a.ifPresent(s -> shout(s));
      }
      return a.map(s -> s.trim())
         .filter(s -> !s.isEmpty())
         .orElseGet(() -> e.orElse(k));
   }

   /** orElseThrow, the last of the six. */
   public String orThrow(String k)
   {
      return findPrivate(k).orElseThrow(() -> new IllegalStateException(k));
   }

   /**
    * The one {@code Optional.get()} in this tree, and the only place in the
    * image that proves optop.get can move at all. The golden solution reads 0
    * here; a counter wired to a constant zero would agree with the golden
    * forever and would pay a tree that moved its nulls into
    * NoSuchElementExceptions in full.
    */
   public String unwrapped(String k)
   {
      Optional<String> o = findPrivate(k);
      return o.isPresent() ? o.get() : "";
   }

   /**
    * The accumulate shape step 04 left standing, in all three operations the
    * counter accepts, with the two shapes it refuses beside them.
    *
    * <p>The sink's DECLARED type is what is recorded, and the two LinkedHashSet
    * sinks below are here to say what that does and does not tell a gate.
    * {@code seen} is declared {@code Set} and constructed {@code LinkedHashSet},
    * which is how the one order-bearing sink in the real tree is written, and
    * its record reads {@code sinktype=java.util.Set} -- so sinktype CANNOT be
    * what decides that {@code Collectors.toSet()} would lose the iteration
    * order there. That is why baseline/sinks.txt carries the wanted factory per
    * bucket, measured at authoring time, instead of being derived from this
    * field. {@code ordered} is declared with its concrete type and reads
    * {@code sinktype=java.util.LinkedHashSet}, which is the same construct
    * spelled the other way and the control that keeps the first assertion from
    * being a statement about the plugin having no idea what a LinkedHashSet is.
    */
   public List<String> accumulate(List<String> src)
   {
      List<String> dst = new ArrayList<>();
      src.stream().map(s -> s.trim()).forEach(dst::add);
      Set<String> seen = new LinkedHashSet<>();
      src.forEach(seen::add);
      LinkedHashSet<String> ordered = new LinkedHashSet<>();
      src.forEach(ordered::add);
      Map<String, String> copy = new HashMap<>();
      explicit.forEach(copy::put);
      src.forEach(s -> copy.put(s, s));
      src.forEach(this::shout);
      return dst;
   }

   /** addAll, the third operation the counter accepts. */
   public void accumAll(List<List<String>> src, List<String> dst)
   {
      src.forEach(dst::addAll);
   }

   /** to=toList, and a pipeline in the same method: C-2 and B-11 both read it. */
   public List<String> collected(List<String> src)
   {
      return src.stream().map(s -> s.trim()).collect(Collectors.toList());
   }

   /** to=toSet. */
   public Set<String> collectedSet(List<String> src)
   {
      return src.stream().collect(Collectors.toSet());
   }

   /** to=toCollection, the one spelling C-2 accepts nothing else in place of. */
   public Set<String> collectedOrdered(List<String> src)
   {
      return src.stream().collect(Collectors.toCollection(LinkedHashSet::new));
   }

   /** to=joining, which is a Collectors factory and still not one of the three. */
   public String joined(List<String> src)
   {
      return src.stream().collect(Collectors.joining(","));
   }

   /**
    * Through a static import: a bare identifier in the AST, the same finding
    * once the element it binds to is resolved.
    */
   public List<String> collectedStatic(List<String> src)
   {
      return src.stream().filter(s -> !s.isEmpty()).collect(toList());
   }

   /** A method merely NAMED collect. */
   String collect(List<String> src)
   {
      return src.toString();
   }

   /** ...and a call to it, whose receiver is this and not a stream. */
   public String notACollect(List<String> src)
   {
      return this.collect(src);
   }

   /**
    * The control for B-11's third sub-gate and this step's C-2 read from
    * opposite ends: a bucket that collects with no pipeline in it. Converting
    * an accumulate site to {@code collect(..)} leaves the producer and the ops
    * where they were, so B-11 reads the bucket as covered afterwards exactly
    * as before; unwinding one back into a loop is what takes the coverage away,
    * and this is that tree, in one method.
    */
   public List<String> unwound(List<String> src)
   {
      List<String> dst = new ArrayList<>();
      for (String s : src)
      {
         dst.add(s.trim());
      }
      return dst;
   }

   /**
    * C-3's accepted pair, in the two members it buckets on: Objects.equals
    * inside equals(Object), Objects.hash inside hashCode().
    */
   @Override
   public boolean equals(Object o)
   {
      if (this == o)
      {
         return true;
      }
      if (!(o instanceof Smoke))
      {
         return false;
      }
      Smoke other = (Smoke) o;
      return Objects.equals(explicit, other.explicit) && Objects.equals(dia, other.dia);
   }

   @Override
   public int hashCode()
   {
      return Objects.hash(explicit, dia);
   }

   /**
    * Objects in a method that is neither. C-3 buckets at &lt;file&gt;#&lt;method&gt;, so a
    * candidate who decorates some other method with requireNonNull collects
    * nothing -- and this call is what proves the bucketing rather than the file
    * is what the gate reads.
    */
   public String required(String k)
   {
      return Objects.requireNonNull(k, "k");
   }

   /** C-4's arrival half: four java.time calls off two owners. */
   public long elapsed(long startMillis)
   {
      Instant start = Instant.ofEpochMilli(startMillis);
      Instant now = Instant.now();
      return Duration.between(start, now).toMillis();
   }

   /**
    * ...and its departure half, in three of the seven shapes millis takes.
    * {@code new java.util.Date()} is not a MethodInvocationTree and is
    * invisible to the counter, which is why baseline/timesites.txt records an
    * independent grep beside the measurement instead of trusting the count.
    */
   public long legacy()
   {
      java.util.Date d = new java.util.Date();
      return System.currentTimeMillis() + System.nanoTime() + d.getTime();
   }

   // ------------------------------------------------------------ step 06
   //
   // The Java 9/10 half. Six library findings and one language feature, and the
   // shape of the rehearsal changes with them: every counter below is read
   // through a discriminating field -- form=, call=, recv=, kind=, arg=, args=
   // -- that a total cannot carry. So each construct here exists to make exactly
   // one of those fields take exactly one of its values, and the near misses
   // live in Lib.java below its own step-06 marker, because every one of them
   // needs a NAME without the ELEMENT and the real imports are in this file.
   //
   // <pre>
   //   newinstance  both forms, and the ctor form in both arities
   //   accessible   all three calls, with recv= taking four values off three
   //                receivers -- the field is the RESOLVED owner and not the
   //                static type of the receiver, and these six records are the
   //                fixture that says so
   //   boxedctor    four wrappers in two buckets, beside the valueOf calls a
   //                name match would take for them
   //   legacycoll   both kinds against all four arg shapes
   //   factory      three owners, both calls, three arities
   //   getpackage   the one-argument static, beside the no-argument instance
   //   optop        Java 9's three operations, and the isPresent() branch they
   //                replace, left standing so the departure half has a subject
   //   var          five inferred declarations in four positions, and the
   //                candidate population beside them: the shapes that cannot be
   //                written with var, and the two declarators of one statement
   //                that var cannot express
   // </pre>

   private final List<String> names = new ArrayList<>();

   /** C-2's departure shape: the Class.newInstance() Java 9 deprecated. */
   Object legacyInstance(String cn) throws Exception
   {
      Class<?> c = Class.forName(cn);
      return c.newInstance();
   }

   /**
    * ...and its arrival shape, in both arities the rewrite produces. The
    * no-argument form is the mechanical one; the form that passes a parameter
    * type is the one that cannot be produced by deleting text, which is why the
    * instruction asks for the exception chain rather than for the spelling.
    */
   Object ctorInstance(String cn) throws Exception
   {
      Class<?> c = Class.forName(cn);
      Object empty = c.getDeclaredConstructor().newInstance();
      Object oneArg = c.getDeclaredConstructor(String.class).newInstance("x");
      return empty == null ? oneArg : empty;
   }

   /**
    * C-3's population: every call the counter takes, off three receivers.
    *
    * <p>recv= is the QUALIFIED OWNER of the resolved method, not the static
    * type of the receiver, and the difference is visible only here.
    * {@code isAccessible} and {@code canAccess} are declared on
    * AccessibleObject alone, so a Field receiver still reports the supertype;
    * {@code setAccessible} is overridden, so the same Field reports Field. Six
    * records, four values of recv=, three receivers -- and a gate that read the
    * receiver's declared type would produce a different six.
    */
   boolean reflectiveAccess() throws Exception
   {
      Field f = Smoke.class.getDeclaredField("explicit");
      Method m = Smoke.class.getDeclaredMethod("io");
      Constructor<?> c = Smoke.class.getDeclaredConstructor();
      AccessibleObject ao = f;
      boolean was = f.isAccessible();
      f.setAccessible(true);
      m.setAccessible(true);
      c.setAccessible(true);
      ao.setAccessible(true);
      return was && f.canAccess(this);
   }

   /** C-4's departure population: three wrappers in one bucket. */
   long boxed()
   {
      Integer i = new Integer(1);
      Long l = new Long(2L);
      Boolean b = new Boolean(true);
      return i.intValue() + l.longValue() + (b ? 1 : 0);
   }

   /**
    * A second bucket, and the near misses beside it. {@code Integer.valueOf} is
    * the arrival shape and is not a construction; a counter matching the
    * wrapper NAME rather than the {@code new} would read five here.
    */
   double notBoxed()
   {
      Double d = new Double(3.0);
      Integer valued = Integer.valueOf(4);
      Character ch = Character.valueOf('c');
      return d + valued + ch;
   }

   /** C-5's view half over a field: the shape that is a live window. */
   List<String> viewOfField()
   {
      return Collections.unmodifiableList(names);
   }

   /** The same call over a local, which is a value and not a window. */
   Set<String> viewOfLocal()
   {
      Set<String> tmp = new LinkedHashSet<>();
      tmp.add("a");
      return Collections.unmodifiableSet(tmp);
   }

   /** ...and over something computed, which resolves to no element at all. */
   Map<String, String> viewOfCall()
   {
      return Collections.unmodifiableMap(new HashMap<>(table));
   }

   /**
    * C-5's build half, in the four argument shapes argShape distinguishes.
    * {@code emptyList()} takes none; a string literal is not an element and
    * reads as computed; {@code Foo.class} is a MemberSelectTree javac resolves
    * to a synthetic FIELD, so it is called out separately as {@code literal};
    * and a parameter reads local.
    */
   List<Object> builds(String[] args)
   {
      List<Object> out = new ArrayList<>();
      out.add(Collections.emptyList());
      out.add(Collections.singletonList("one"));
      out.add(Arrays.asList(Smoke.class, Lib.class));
      out.add(Arrays.asList(args));
      return out;
   }

   /** C-5's arrival half: three owners, both calls, three arities. */
   int factories(List<String> src)
   {
      List<String> three = List.of("a", "b", "c");
      Set<String> one = Set.of("a");
      Map<String, String> pair = Map.of("k", "v");
      List<String> copy = List.copyOf(src);
      Map.Entry<String, String> e = Map.entry("k", "v");
      Map<String, String> entries = Map.ofEntries(e);
      return three.size() + one.size() + pair.size() + copy.size() + entries.size();
   }

   /**
    * The one-argument static getPackage, and the no-argument instance call
    * beside it. Only the first is a record: the counter asks for the owner AND
    * the arity, because {@code getClass().getPackage()} is not the call Java 9
    * deprecated and rewriting it buys nothing.
    */
   String packages()
   {
      Package byName = Package.getPackage("smoke");
      Package ofClass = getClass().getPackage();
      return (byName == null ? "-" : byName.getName())
         + (ofClass == null ? "-" : ofClass.getName());
   }

   /** C-6's arrival: the three Java 9 operations that remove an isPresent(). */
   String java9Optional(String k)
   {
      Optional<String> o = findPrivate(k);
      o.ifPresentOrElse(v -> table.put(k, v), () -> table.remove(k));
      return o.or(() -> Optional.of("fallback")).orElse("");
   }

   /**
    * ...and the branch they replace, left standing so that the departure half
    * has a subject on this tree. stream() is the third operation and is a use
    * like any other, which is what keeps C-6's arrival set at three names
    * rather than at two names and a special case.
    */
   long stillBranching(String k)
   {
      Optional<String> o = findPrivate(k);
      if (o.isPresent())
      {
         return 1;
      }
      return o.stream().count();
   }

   /** C-7's arrival: var in the four declaration positions that accept it. */
   int inferred(List<String> src) throws Exception
   {
      var total = 0;
      var joined = new StringBuilder();
      for (var s : src)
      {
         joined.append(s);
         total += s.length();
      }
      for (var i = 0; i < src.size(); i++)
      {
         total += i;
      }
      try (var in = new FileInputStream("y"))
      {
         total += in.available();
      }
      return total + joined.length();
   }

   /**
    * ...and C-7's denominator: the declarations that could be written with var
    * and the ones that could not.
    *
    * <p>Two are candidates by construction and one by a call whose return type
    * is written out again. The rest are the exclusions, one apiece: a declared
    * type that is not the initialiser's type, a null literal that has no
    * denotable type, a lambda that has no type until a target supplies one, an
    * array initialiser that is syntax rather than an expression, an anonymous
    * body that would infer the anonymous type instead of the named supertype,
    * and two declarators over one written type, which var cannot express at
    * all and which are counted as multideclarator rather than dropped silently.
    */
   int candidates(String k)
   {
      ArrayList<String> list = new ArrayList<>();
      StringBuilder sb = new StringBuilder();
      String trimmed = k.trim();
      List<String> widened = new ArrayList<>();
      String nothing = null;
      Runnable poly = () -> list.clear();
      int[] array = { 1, 2 };
      Object body = new Object() { };
      int a = 1, b = 2;
      sb.append(trimmed).append(nothing).append(widened.size()).append(array.length);
      poly.run();
      return list.size() + sb.length() + body.hashCode() + a + b;
   }

   // ------------------------------------------------------------ step 07
   //
   // The Java 11 + jakarta half. Three record blocks that did not exist below
   // this rung -- ns, toarray, http -- and every one of them is read by family
   // C through a field the total cannot carry:
   //
   // <pre>
   //   ns        pkg= is the first TWO segments of the RESOLVED qualified name.
   //             So javax.lang.model is a javax package and is not one of the
   //             six this step has an opinion about, and the scanner does not
   //             decide which six: C-2 asks about two prefixes, C-3 asks about
   //             six, and both read the same bucketing. Two channels, because
   //             an import-only reading misses a fully-qualified use and a
   //             reference-only reading misses an import that a deletion left
   //             behind.
   //   toarray   form=sized against form=ctorref, and the arrival half is a
   //             MODE and not a name -- javac calls a constructor reference
   //             <init>, so a name test reports 0 on a tree holding eight, and
   //             0 is also what the unmigrated tree says.
   //   http      four forms over two channels -- two resolved TYPE references
   //             and six METHOD NAMES -- because a tree that has deleted the
   //             type has also deleted the receiver an owner-scoped counter
   //             would have read the owner from.
   // </pre>
   //
   // Both namespaces are on this tree at once, which is the state a half-done
   // migration is in and the one the gate has to be able to read. The near
   // misses are in Lib.java below its own step-07 marker: a package of one's
   // own called javax.something cannot be written here, but a method named
   // toArray and a method named disconnect can be, and those two are the
   // instrument's real edges.

   /**
    * ns, the annotation channel. An annotation use is an IDENTIFIER and not a
    * member select, so {@code @Resource} reaches the scanner through
    * visitIdentifier and nowhere else -- and annotations are most of what this
    * rung moves. Both are FIELD annotations with no enclosing method, so both
    * records carry {@code in=-}, which is the bucket eleven of the real tree's
    * seventeen javax.annotation references live in.
    */
   @javax.annotation.Resource(name = "held")
   private String legacyResource;

   @jakarta.annotation.Resource(name = "moved")
   private String movedResource;

   /**
    * ns: the two prefixes C-2 moves, one method holding both sides of the move
    * at once. The ejb pair is written out in full rather than imported, which
    * is the reference channel with no import behind it.
    */
   String namespaceMove()
   {
      javax.ejb.EJBException legacy = new javax.ejb.EJBException("old");
      jakarta.ejb.EJBException moved = new jakarta.ejb.EJBException("new");
      return legacy.getMessage() + moved.getMessage()
         + legacyResource + movedResource;
   }

   /**
    * ns: the JDK javax packages, which are still spelled javax in Java 25.
    * A candidate who "finishes the namespace migration" by rewriting these
    * produces a tree that does not compile, and C-3's 43 held pairs are the
    * instrument that says so before the compile does. Three packages in one
    * method, which is also the fixture for C-3's key being the PAIR and not the
    * bucket: a bucket-keyed hold would still read full after two of the three
    * moved.
    */
   String jdkNamespaces() throws Exception
   {
      javax.naming.Context ctx = new javax.naming.InitialContext();
      javax.management.ObjectName on = new javax.management.ObjectName("a:b=c");
      javax.xml.namespace.QName qn = new javax.xml.namespace.QName("q");
      return ctx.getNameInNamespace() + on.getCanonicalName() + qn.getLocalPart();
   }

   /**
    * ns: javax.lang.model is javax, is not enterprise, and is not one of the
    * six. Its own bucket, so a gate that tested {@code startsWith("javax.")}
    * and stopped there would see this method and would be wrong about it.
    */
   String notEnterprise(javax.lang.model.element.Element e)
   {
      return e.getSimpleName().toString();
   }

   /**
    * toarray, the departure half: the pre-11 spelling in both of its shapes.
    * The zero-length array and the sized one are the same form to this counter
    * -- the argument is a NewArrayTree either way -- and that is deliberate,
    * because C-4 asks for the call site to stop allocating at all rather than
    * for one of the two to become the other.
    */
   String[] sizedArrays()
   {
      List<String> a = new ArrayList<String>();
      a.add("x");
      String[] empty = a.toArray(new String[0]);
      String[] sized = a.toArray(new String[a.size()]);
      return empty.length > sized.length ? empty : sized;
   }

   /**
    * toarray, the arrival half, and the raw-element edge beside it.
    *
    * <p>{@code String[]::new} is the shape C-4 pays for. {@code Optional[]::new}
    * also compiles, is a raw array type, and no {@code new} expression appears
    * anywhere in it -- so {@code newgeneric.raw}, the counter that catches every
    * other raw shortcut on this tree, cannot see it. {@code toarray.raw} is the
    * only channel that does, and this is the one construct on the smoke tree
    * that makes it read anything but zero.
    */
   Object ctorRefArrays()
   {
      List<String> a = new ArrayList<String>();
      List<Optional<String>> b = new ArrayList<Optional<String>>();
      String[] written = a.toArray(String[]::new);
      Optional[] raw = b.toArray(Optional[]::new);
      return written.length + raw.length;
   }

   /**
    * toarray's near misses, in the file where the elements resolve. The
    * no-argument {@code toArray()} is not a one-argument call and is not
    * counted; {@code Arrays.copyOf} is the same idea under another name and is
    * not counted either. A counter that matched on arity alone or on "returns
    * an array" would move on both.
    */
   int notToArray()
   {
      List<String> a = new ArrayList<String>();
      Object[] plain = a.toArray();
      String[] copied = Arrays.copyOf(new String[] { "x" }, 1);
      return plain.length + copied.length;
    }

   /**
    * http, the departure half: every one of the six legacy call names and both
    * legacy type names, in the shape ServletMethodExecutor#execute holds them.
    * The method is never run -- this tree is compiled and scanned, never
    * executed -- so the loop that would poll for a 200 is written as the single
    * pass that carries the calls.
    */
   int legacyHttp(String url) throws Exception
   {
      java.net.URLConnection conn = new java.net.URL(url).openConnection();
      if (!(conn instanceof java.net.HttpURLConnection))
      {
         return -1;
      }
      java.net.HttpURLConnection http = (java.net.HttpURLConnection) conn;
      http.setUseCaches(false);
      http.setDefaultUseCaches(false);
      int code = http.getResponseCode();
      String msg = http.getResponseMessage();
      http.disconnect();
      return code + msg.length();
   }

   /**
    * http, the arrival half: the three java.net.http types C-5 pays for, and
    * {@code followRedirects}, which is the builder call that has no pre-11
    * counterpart and is therefore its own form rather than a legacy call by
    * another name.
    *
    * <p>This compiles at -source 10 as well as at 11, and that is a fact about
    * the gate rather than an accident: source-gate.sh passes -source/-target
    * and never --release, so the platform classes are the image's own JDK 11
    * ones at every level. The level assertion this rung makes is C-1's, read
    * out of a published class-file header, and not this one.
    */
   int modernHttp(String url) throws Exception
   {
      java.net.http.HttpClient client = java.net.http.HttpClient.newBuilder()
         .followRedirects(java.net.http.HttpClient.Redirect.NORMAL)
         .build();
      java.net.http.HttpRequest req = java.net.http.HttpRequest.newBuilder()
         .uri(java.net.URI.create(url)).build();
      java.net.http.HttpResponse<String> res = client.send(req,
         java.net.http.HttpResponse.BodyHandlers.ofString());
      return res.statusCode() + res.body().length();
   }
}
