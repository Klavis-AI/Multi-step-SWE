package smoke;

// ------------------------------------------------------------------ step 13
// The fourth file of this smoke tree, and the only one with a raw type in it.
// It is separate for a different reason than Mod.java is. Mod.java is its own
// file because an import declaration sits above every section marker and no
// cut can reach one; this file is its own file because flatten() writes
// Smoke.java and Lib.java BY NAME, so a file it has never heard of is off all
// eight cut trees for free -- and that is what let step 13 add a record block
// without adding a cut. Every figure below the cut banner in src-rehearsal.sh
// is the figure step 12 measured.
//
// NINE DECLARATIONS, THREE OF THEM FORCED, over five buckets. Smoke.java has
// two more of its own that are left where they are, so rawdecl reads 11 rows
// in 2 files over 7 buckets -- and the file half of a bucket key is exercised
// because the population is not all in one file.
//
// WHAT forced MEANS, and it is the half no hand-written report could rehearse:
// the declaration cannot be narrowed without breaking something the tree does
// not own. Two of the three come out of a JAR ON THE SHELF -- TestNG 5.14.6's
// IAnnotationTransformer, reached through the gate's SRCCP -- and the third
// comes out of an interface in this file, so both paths to forced-ness are
// walked. The seed's own forced population is four rows of the first shape.

public class Raw implements org.testng.IAnnotationTransformer, Source
{
   int hits;

   // TWO RAW FIELDS, ONE BUCKET. enclosingMethod() has nothing to return
   // outside a method, so both land in <file>#- and cbuckets() counts the key
   // twice. That collapse shrinks C-3's denominator rather than inflating it,
   // and four of the seed's six open buckets have the same shape.
   java.util.List rows;

   // AND THE SECOND ONE IS NOT AT THE TOP OF ITS TYPE. The raw List is an
   // ARGUMENT of the Map, depth=1, which a walk that only looked at the
   // written type's head would miss. The Map itself is parameterized and is
   // not a row.
   java.util.Map<String, java.util.List> byName;

   // A near miss, and the commonest one there is: parameterized, so not raw.
   java.util.Set<String> kept;

   // FORCED, TWICE, BY A CLASS FILE. IAnnotationTransformer declares
   // transform(ITestAnnotation, Class, Constructor, Method) with Class and
   // Constructor written raw, so an implementation that narrowed them would
   // stop overriding anything. The reference solution for the seed suppresses
   // the warning and keeps the parameters, which is why the same rows appear
   // in suppress.txt's allow half.
   //
   // AND THE FOURTH PARAMETER IS THE NEAR MISS INSIDE THE FORCED METHOD.
   // java.lang.reflect.Method declares no type parameters at all, so it is not
   // raw and is not a row. A rule keyed on "parameter of an overriding method"
   // rather than on the type would count three here.
   public void transform(org.testng.annotations.ITestAnnotation ann,
                         Class testClass,
                         java.lang.reflect.Constructor testCtor,
                         java.lang.reflect.Method testMethod)
   { hits++; }

   // OPEN, and in a method of its own so the bucket set has more than one
   // shape in it: a raw parameter that overrides nothing can be narrowed by
   // whoever owns this file.
   void take(java.util.Comparator order) { hits++; }

   // TWO ROWS IN ONE BUCKET, at two declaration kinds: a raw return type and a
   // raw local. Both open, both in=names.
   java.util.Set names() { java.util.Iterator cursor; return kept; }

   // FORCED BY A SUPERTYPE IN THIS FILE rather than by one in a jar. Source
   // below declares all() returning a raw List; this overrides it, so this row
   // is forced and that one is open -- AND THEY SHARE A BUCKET, because both
   // are in=all and both are in Raw.java. One key, in the open set and in the
   // forced set at once. The seed has no bucket like it.
   public java.util.List all() { return rows; }

   // A CAST IS NOT A DECLARATION. javac reports this under [rawtypes] and
   // rawdecl does not have a row for it, which is one of the two directions
   // the two instruments disagree in.
   void nearMisses(Object o)
   { Object cast = (java.util.List) o; hits += cast == null ? 0 : 1; }
}

// The supertype whose raw return type forces Raw#all() above. Open in its own
// right: nothing forces this one.
interface Source { java.util.List all(); }

// A RAW SUPERTYPE, AND THE OTHER DIRECTION. javac reports the implements
// clause under [rawtypes]; rawdecl does not, because a supertype is what makes
// other sites forced rather than a site anyone can narrow on its own, and
// counting it would put a row in C-3's denominator that no migration can move.
// The compare() parameters are Objects, not raw types, and are not rows.
class RawSub implements java.util.Comparator
{ public int compare(Object a, Object b) { return 0; } }
