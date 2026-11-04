package smoke;

// ------------------------------------------------------------------ step 12
// The third file of this smoke tree, and the only one with a module import in
// it. Everything C-2 and C-3 read is here, because both are properties of an
// import block and an import block cannot be cut by a section marker: the sed
// address every other cut uses runs from a marker to the end of the file, and
// an import is above every marker there is. This file is cut by being absent.
//
// THE FIVE IMPORTS BELOW ARE FIVE DIFFERENT ANSWERS, one per retention class
// the gate distinguishes, and between them they move every jdkimport counter
// the scanner publishes. The seed's own answer is 452 departures and 43 holds
// -- 25 ambiguous, 16 static, 2 nested -- so three of the four hold reasons
// have a row here and the fourth, the on-demand import, has one too even
// though the seed has none: a counter the scanner publishes and no tree in the
// image ever moves is a counter nobody would notice losing.

import module java.base;

// HELD, ambiguous. Three packages java.base exports declare a public type
// called Annotation -- java.lang.annotation, java.lang.classfile and java.text
// -- so the module import cannot supply the name and javac says "reference to
// Annotation is ambiguous" rather than picking one. This is the seed's own
// ambiguity, 25 rows of it, and it is the reason C-3's held half is not a
// rounding error.
import java.lang.annotation.Annotation;

// HELD, nested. A module import supplies the PACKAGE MEMBERS of every package
// a module exports, and a nested type is a member of a type. Two rows in the
// seed, both this exact import.
import java.util.ServiceLoader.Provider;

// HELD, static. A static import names a member of a type, which no module
// import supplies either -- and this is the seed's commonest hold, 16 rows,
// every one of them a RetentionPolicy constant in an annotation declaration.
import static java.lang.annotation.RetentionPolicy.RUNTIME;

// ON DEMAND. Subsumed in substance -- java.base exports java.util.zip and the
// module import already supplies CRC32 -- but a different declaration from a
// single-type import, and the scanner counts it apart. No row in the seed has
// this shape; this one exists so that the counter is exercised by something.
import java.util.zip.*;

// AND ONE THAT SHOULD HAVE GONE. java.util.List is supplied by the module
// import above, on its own, unambiguously: this row is a departure the
// migration did not make. It is here because the gate's whole job at C-3 is to
// SEE the import that stayed behind, and a smoke tree in which every
// subsumable import had been removed would exercise the counter only on rows
// that are absent.
import java.util.List;

// AND ONE THAT IS NOT A JDK IMPORT AT ALL, which is the near miss every other
// block in this rehearsal has and this one would otherwise go without.
// org.testng lives on the classpath -- SRCCP puts testng-5.14.6.jar there --
// rather than in a module the JDK ships, so Jdk.packageOf() hands back null and
// the import is skipped before any retention class is decided for it. The seed
// is full of imports this shape: org.jboss.*, org.testng.*, and they are the
// reason C-3's population is 495 rows and not the several thousand import
// declarations 171 files actually carry. A scanner that counted every import
// would score the departure half against a denominator nobody can move.
import org.testng.annotations.Test;

/**
 * The file that has done C-2's work, and most of C-3's.
 *
 * <p>Every unqualified JDK name below that is not one of the five imports is
 * supplied by the module import: {@code ArrayList}, {@code Retention},
 * {@code ServiceLoader}, {@code CRC32} and {@code StandardCharsets} all come
 * in through {@code import module java.base}. That is the arrival side of the
 * leg, and it is what makes the file compile at 25 and not at 24.
 */
public class Mod
{
   /**
    * The ambiguous name, used. {@code Annotation} resolves through the
    * single-type import; {@code Retention} resolves through the module import.
    * Two names from the same package, arriving two different ways, in one
    * expression -- which is the state a half-subsumed import block is actually
    * in.
    */
   public static boolean runtimeRetained(Annotation a)
   {
      Retention r = a.annotationType().getAnnotation(Retention.class);
      return r != null && r.value() == RUNTIME;
   }

   /**
    * The nested name, used -- and the one place on this tree where a module
    * import LOSES.
    *
    * <p>{@code Provider} arrives through the single-type import above.
    * {@code ServiceLoader} is written out in full because the unqualified name
    * in this package is not java.util's: Lib.java declares a
    * {@code smoke.ServiceLoader} of its own, step 08's near miss, and a type in
    * the current package beats a module import the way it beats an on-demand
    * one. A migration that replaced this file's imports with
    * {@code import module java.base} and left the parameter unqualified would
    * compile -- against the wrong type, silently, with a diagnostic only if the
    * two happen to have different members. So the qualification is the fixture,
    * not the tidiness.
    */
   public static List<String> providerTypes(java.util.ServiceLoader<CharSequence> loader)
   {
      List<String> out = new ArrayList<String>();
      for (Provider<CharSequence> p : loader.stream().toList())
      {
         out.add(p.type().getName());
      }
      return out;
   }

   /**
    * The on-demand name, used. {@code CRC32} would resolve without the
    * {@code java.util.zip.*} import at the top of this file, which is exactly
    * why that import is the kind a migration is expected to drop.
    */
   @Test
   public static long checksum(String text)
   {
      CRC32 crc = new CRC32();
      crc.update(text.getBytes(StandardCharsets.UTF_8));
      return crc.getValue();
   }
}
