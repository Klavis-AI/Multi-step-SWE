package smoke;

import java.time.LocalDate;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collector;
import java.util.stream.Collectors;

/**
 * The impostors, and the second file.
 *
 * <p>Two jobs, and the second one is the reason this is a file rather than
 * another section of Smoke.java. {@code retnull.files}, {@code optret.files}
 * and {@code jtime.files} are distinct-file counts, and on a one-file tree
 * every one of them reads 1 whatever {@code distinctFiles} actually does --
 * including "return the number of records" and "return 1". The gates above
 * them bucket at &lt;file&gt;#&lt;method&gt;, so a one-file rehearsal would hold the file
 * component constant through every set intersection it checked. Two files is
 * the smallest tree on which those two things are measured rather than assumed.
 *
 * <p>The first job is the near misses that only a second file can hold. Every
 * counter in family C resolves through javac's elements rather than through the
 * name a call is spelled with, and the way to show that is a tree containing
 * the names without the elements:
 *
 * <pre>
 *   Optional   this file declares its own, so the simple name in this file
 *              means Lib.Optional. Its get(), isPresent() and orElse() must
 *              produce no optop record at all -- and the REAL Optional is
 *              reachable here only fully qualified, which is one more spelling
 *              optret has to resolve to the same finding.
 *   Objects    likewise, and Lib.Objects.equals/hash must produce no objects
 *              record. java.util.Objects, fully qualified, must produce two.
 *   Collectors Impostor.toList() returns a real Collector to a real stream's
 *              collect(..), so the receiver check passes and only the FACTORY's
 *              owner tells the two apart. It must read to=other.
 * </pre>
 *
 * <p>And C-3's two near misses, which are the whole of that gate's shape.
 * {@code Legacy} hand-rolls equals and hashCode with no Objects call in either
 * -- the state the three real value types start in. {@code Halfway} calls
 * java.util.Objects in BOTH members and still must not count, because its
 * hashCode calls {@code Objects.hashCode} and not {@code Objects.hash}:
 * hashCode(Object) is the null-safe single-value form and a hashCode built out
 * of it still hand-rolls the 31 * result loop. That is the one place in family
 * C where a method NAME is matched rather than an element resolved, and this is
 * the fixture that decides whether the match is the right one.
 *
 * <p>Both classes are in this file, so both contribute to the SAME
 * {@code Lib.java#equals} and {@code Lib.java#hashCode} buckets. That is a real
 * property of bucketing at the file and not at the class, it is stated here so
 * that it is a known one, and it does not reach the gate: each of the three
 * files baseline/valuetypes.txt names declares exactly one class with an
 * equals/hashCode pair, which is what &lt;file&gt;#&lt;method&gt; assumes.
 */
final class Lib
{
   private final Map<String, String> table = new HashMap<>();

   // --------------------------------------------------------- the impostors

   /** This file's own Optional. Not java.util's, and not one optop record. */
   static final class Optional
   {
      private final String v;

      Optional(String v)
      {
         this.v = v;
      }

      String get()
      {
         return v;
      }

      boolean isPresent()
      {
         return v != null;
      }

      String orElse(String other)
      {
         return v == null ? other : v;
      }
   }

   /** This file's own Objects. Not java.util's, and not one objects record. */
   static final class Objects
   {
      static boolean equals(Object a, Object b)
      {
         return a == b;
      }

      static int hash(Object a)
      {
         return a == null ? 0 : a.hashCode();
      }
   }

   /**
    * A collector factory that is not Collectors. It returns a real Collector,
    * so the call below type-checks and the receiver check passes: the owner of
    * the factory is the only thing that tells this from the real article.
    */
   static final class Impostor
   {
      static Collector<String, ?, List<String>> toList()
      {
         return Collectors.toList();
      }
   }

   /** Three calls that look exactly like optop records and are not. */
   String impostors()
   {
      Optional o = new Optional("x");
      if (o.isPresent())
      {
         return o.get();
      }
      return o.orElse("");
   }

   /** And two that look exactly like objects records and are not. */
   boolean impostorObjects(String a, String b)
   {
      return Objects.equals(a, b) && Objects.hash(a) == 0;
   }

   // -------------------------------------------------- and the real articles

   /**
    * The real Optional, fully qualified because the simple name in this file is
    * taken. One optret record, with ret= printed as written and the comparison
    * made on the resolved element.
    */
   java.util.Optional<String> findQualified(String k)
   {
      return java.util.Optional.ofNullable(table.get(k));
   }

   /** The second file's retnull site, so retnull.files is not 1. */
   private String lookupHere(String k)
   {
      if (table.containsKey(k))
      {
         return table.get(k);
      }
      return null;
   }

   /** The second file's java.time calls, so jtime.files is not 1. */
   int dayOfYear()
   {
      return LocalDate.now().getDayOfYear();
   }

   /**
    * A legacy clock shape that is neither System.currentTimeMillis nor
    * System.nanoTime: format(Date) resolves to java.text.DateFormat, which is
    * one of the five legacy types the counter also takes.
    */
   String stamp()
   {
      return new java.text.SimpleDateFormat("yyyy").format(new java.util.Date());
   }

   /** to=other by OWNER: a real stream, a real Collector, the wrong factory. */
   List<String> viaImpostor(List<String> src)
   {
      return src.stream().collect(Impostor.toList());
   }

   /** to=other by ARITY: the three-argument collect names no factory at all. */
   List<String> viaThreeArg(List<String> src)
   {
      return src.stream().collect(ArrayList::new, List::add, List::addAll);
   }

   /** An accumulate site in the second file, so accum.methods spans two. */
   List<String> gather(List<String> src)
   {
      List<String> out = new ArrayList<>();
      src.forEach(out::add);
      return out;
   }

   // ------------------------------------------------- C-3's two near misses

   /** Hand-rolled equals and hashCode. Not one Objects call in either. */
   static final class Legacy
   {
      private final String name;

      Legacy(String name)
      {
         this.name = name;
      }

      @Override
      public boolean equals(Object o)
      {
         if (this == o)
         {
            return true;
         }
         if (!(o instanceof Legacy))
         {
            return false;
         }
         Legacy other = (Legacy) o;
         if (name == null)
         {
            return other.name == null;
         }
         return name.equals(other.name);
      }

      @Override
      public int hashCode()
      {
         return name == null ? 0 : name.hashCode();
      }
   }

   /**
    * java.util.Objects in both members, and still not migrated: hashCode calls
    * Objects.hashCode, which combines nothing, so the 31 * result loop is still
    * hand-rolled underneath it.
    */
   static final class Halfway
   {
      private final String name;

      private final int order;

      Halfway(String name, int order)
      {
         this.name = name;
         this.order = order;
      }

      @Override
      public boolean equals(Object o)
      {
         if (this == o)
         {
            return true;
         }
         if (!(o instanceof Halfway))
         {
            return false;
         }
         Halfway other = (Halfway) o;
         return java.util.Objects.equals(name, other.name) && order == other.order;
      }

      @Override
      public int hashCode()
      {
         return 31 * java.util.Objects.hashCode(name) + order;
      }
   }
}

// ---------------------------------------------------------------- step 06
//
// The step-06 near misses, and the second file's share of the step-06 records.
//
// Top-level classes rather than members of Lib, and that is not style: the
// flatten control cuts this file at the line above with one sed address, and a
// member would leave a brace to balance. Everything below is package-private,
// so the file still declares no public type but its own name.

/**
 * The names without the elements. Every step-06 counter resolves through
 * javac's elements -- the owner of the resolved method, the created type, the
 * arity -- and the way to show that is a tree holding the names and meaning
 * something else by all of them.
 *
 * <pre>
 *   Integer      a wrapper of this file's own. `new Integer(3)` here is not a
 *                boxedctor site, and a counter matching the written type is
 *                exactly what that would be
 *   Handle       newInstance() in both arities Class and Constructor have
 *   Slot         setAccessible, isAccessible and canAccess on a class that is
 *                not an AccessibleObject and never could be
 *   Bag          static of(..) and copyOf(..) on an owner FACTORY_OWNERS does
 *                not name
 *   Collections  and a Collections of one's own, with the wrapping call and
 *                the empty factory spelled exactly
 * </pre>
 */
final class Impostor06
{
   /** This file's own Integer. Constructing it is not constructing a wrapper. */
   static final class Integer
   {
      private final int v;

      Integer(int v)
      {
         this.v = v;
      }

      int get()
      {
         return v;
      }
   }

   /** newInstance(), in both arities -- neither on Class nor on Constructor. */
   static final class Handle
   {
      Object newInstance()
      {
         return new Object();
      }

      Object newInstance(String which)
      {
         return which;
      }
   }

   /** The three accessible calls, on a receiver the owner set does not name. */
   static final class Slot
   {
      private boolean open;

      void setAccessible(boolean flag)
      {
         open = flag;
      }

      boolean isAccessible()
      {
         return open;
      }

      boolean canAccess(Object o)
      {
         return open && o != null;
      }
   }

   /** of(..) and copyOf(..), static, on something that is not List/Set/Map. */
   static final class Bag
   {
      static Bag of(String a, String b)
      {
         return new Bag();
      }

      static Bag copyOf(Bag other)
      {
         return new Bag();
      }
   }

   /** unmodifiableList and emptyList, on an owner that is not java.util's. */
   static final class Collections
   {
      static List<String> unmodifiableList(List<String> in)
      {
         return in;
      }

      static List<String> emptyList()
      {
         return new ArrayList<>();
      }
   }

   /** Every one of them called, in one bucket, and not one of them a record. */
   int impostors06(List<String> src)
   {
      Integer i = new Integer(3);
      Handle h = new Handle();
      Slot s = new Slot();
      s.setAccessible(true);
      Bag b = Bag.of("a", "b");
      return i.get() + h.newInstance().hashCode() + (s.isAccessible() ? 1 : 0)
         + (s.canAccess(b) ? 1 : 0) + Bag.copyOf(b).hashCode()
         + Collections.unmodifiableList(src).size()
         + Collections.emptyList().size() + h.newInstance("x").hashCode();
   }

   /**
    * getPackage() with no argument: the same name, on the wrong owner and at
    * the wrong arity. Java 9 deprecated the static lookup by name and left this
    * one alone, so rewriting it buys nothing and the counter must not ask.
    */
   String noArgPackage()
   {
      Package p = getClass().getPackage();
      return p == null ? "-" : p.getName();
   }
}

/**
 * The second file's share of the step-06 records, so that no .files counter in
 * this step's half reads 1 whatever distinctFiles() does. Seven families, one
 * site apiece, and every one of them the real article.
 */
final class Real06
{
   private final List<String> backing = new ArrayList<>();

   /** legacycoll, kind=view arg=field, in the file that is not Smoke.java. */
   List<String> view()
   {
      return java.util.Collections.unmodifiableList(backing);
   }

   /** newinstance in both forms, and one accessible call beside them. */
   Object made() throws Exception
   {
      Object viaClass = Class.forName("smoke.Lib").newInstance();
      Object viaCtor = Class.forName("smoke.Lib").getDeclaredConstructor().newInstance();
      java.lang.reflect.Field f = Lib.class.getDeclaredField("table");
      f.setAccessible(true);
      return viaClass.hashCode() + viaCtor.hashCode() + f.getName().length();
   }

   /** boxedctor, factory, getpackage and var, one apiece. */
   int boxedHere()
   {
      Short s = new Short((short) 1);
      var built = List.of("a");
      Package p = Package.getPackage("smoke");
      return s.intValue() + built.size() + (p == null ? 0 : p.getName().length());
   }
}

// ---------------------------------------------------------------- step 07
//
// The step-07 near misses. Top-level classes for the same reason step 06's
// are: the flatten control cuts this file at the line above with one sed
// address, and a member would leave a brace to balance.
//
// Every counter this rung adds resolves through javac's elements EXCEPT two,
// and the two are here rather than described in a comment. `toarray` matches a
// one-argument call whose selected identifier is `toArray`, and `http`'s call
// half matches six bare method names -- neither reads an owner, and neither
// can, because the tree that has finished the migration has deleted the
// receiver an owner would be read off. So both counters are NAME-SCOPED over
// the whole tree, and these classes are the measurement of what that costs.
// The frozen figures in src-rehearsal.sh include these sites, and the
// population files -- toarray.txt's five buckets, http.txt's named file -- are
// what scopes the GATE back down to the real ones.

/**
 * A toArray of one's own: one argument, the right name, nothing to do with a
 * collection. It is counted, and saying so here is the point -- an instrument
 * whose edge is discovered by a candidate is an instrument that was never
 * measured.
 */
class Arrayish
{
   static String[] toArray(String[] seed)
   {
      return seed;
   }

   /** ...and the two-argument overload, which is not one argument and is not counted. */
   static String[] toArray(String[] seed, int n)
   {
      return n > 0 ? seed : new String[0];
   }

   int use()
   {
      String[] one = Arrayish.toArray(new String[] { "a" });
      String[] two = Arrayish.toArray(new String[] { "a" }, 1);
      return one.length + two.length;
   }
}

/**
 * A disconnect of one's own, and a setUseCaches of one's own, against a
 * receiver that is not a connection of any kind. Both are counted as legacy
 * HTTP calls; the gate that reads them is scoped to one FILE by http.txt, which
 * is the answer to that and is stated in http.txt rather than left implicit.
 *
 * <p>followRedirects is here too, and it lands in the OTHER form -- so a tree
 * with no HTTP in it at all can still move http.redirects, and C-5's arrival
 * half reads the java.net.http TYPE references rather than this.
 */
class Wire
{
   void disconnect()
   {
   }

   void setUseCaches(boolean b)
   {
   }

   Wire followRedirects(String policy)
   {
      return policy == null ? this : this;
   }

   int use()
   {
      Wire w = new Wire();
      w.setUseCaches(false);
      w.followRedirects("NORMAL");
      w.disconnect();
      return 1;
   }
}

/**
 * A class in a package whose name merely BEGINS with the four letters. The ns
 * counter buckets on the first two segments of a qualified name that starts
 * with `javax.` or `jakarta.`, and `javaxml` starts with neither -- but a gate
 * that had written `startsWith("javax")` without the dot would bucket this one.
 * Named rather than packaged, because a second package on a two-file smoke tree
 * would change every file-scoped counter below this rung.
 */
class javaxish
{
   String name()
   {
      return "javaxish";
   }
}
