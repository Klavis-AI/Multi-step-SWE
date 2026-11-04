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
