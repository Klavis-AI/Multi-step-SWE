package arqgate;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import org.objectweb.asm.AnnotationVisitor;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.Type;

/**
 * The test-framework half of the step-2 migrated-ness gates: which test
 * vocabulary does each compiled class actually speak?
 *
 * <pre>
 *   java -cp arqgate.jar:asm.jar:asm-tree.jar arqgate.Junit5 &lt;out.txt&gt; &lt;jar-or-dir&gt;...
 * </pre>
 *
 * <p><b>It measures, and never decides.</b> Same contract as {@link Scan} and
 * {@link Tests}: no watch list, no threshold, no notion of which classes are
 * allowed to still speak JUnit 4. The step's verifier owns every expectation,
 * so one tool can serve a ladder whose later steps have different exceptions.
 *
 * <p><b>Why a separate tool from {@link Scan}.</b> Scan is deliberately limited
 * to the platform namespaces -- {@code java}, {@code javax}, {@code jakarta},
 * {@code sun}, {@code com.sun}, {@code jdk} -- because every gate it feeds is
 * about the JDK. {@code org/junit} is a third-party namespace and is invisible
 * to it. Widening Scan would change a payload that eleven other steps already
 * ship and that has already been reviewed; a second tool costs one file.
 *
 * <p><b>Why "test class" is decided from annotations and not from a path.</b>
 * The obvious rule is "everything under {@code build/classes/java/test}", and it
 * is the candidate who chooses that path -- a tree that compiled its tests into
 * the main source set would read as having no tests at all and pass every gate
 * about them vacuously. A test method is a method with a test annotation on it
 * whoever compiled it and wherever they put it, so that is what is read. The
 * net is widened once, to classes that call an assertion API without carrying an
 * annotation, because a shared test base class that holds only helper methods is
 * a real shape in this tree and is exactly where a migration can be left half
 * done.
 *
 * <p><b>The one thing this cannot see.</b> A message moved to the wrong argument
 * position -- JUnit 4 puts it first, JUnit 5 puts it last -- survives erasure:
 * a mis-migrated {@code assertEquals("msg", expected, actualString)} compiles to
 * the same {@code (Object,Object,String)} descriptor as the correct
 * {@code assertEquals(expected, actual, "msg")}. Telling those apart needs a
 * def-use walk of the operand stack, which needs {@code asm-analysis}. It is
 * not here, and the reason is measured rather than assumed: on this tree the
 * swap does not compile at any site but {@code assertNotNull}, every
 * {@code assertNotNull} site's subject is a non-String, and at every other
 * assertion the swap either fails to compile or fails at run time and is caught
 * by the inherited "every baseline test still runs and still passes" gate. What
 * is checkable cheaply, and is checked here, is that the messages are still
 * *there*: {@code j5.call.jupiter.msg} against the recorded legacy count.
 *
 * <p>Output, sorted, one fact per line:
 *
 * <pre>
 *   j5.seen=&lt;binary&gt;                  a class that was read at all
 *   j5.major=&lt;binary&gt;|&lt;n&gt;             ... and the class-file major version it
 *                                     was compiled to
 *   j5.test=&lt;binary&gt;|&lt;frameworks&gt;     a class carrying test annotations
 *   j5.asserts=&lt;binary&gt;|&lt;families&gt;    a class calling an assertion API
 *   j5.legacyref=&lt;binary&gt;|&lt;type&gt;      a class naming a retired JUnit type
 *   j5.legacytest=&lt;binary&gt;            both of the above, in one class
 *   j5.expected=&lt;binary&gt;#&lt;method&gt;     an @org.junit.Test(expected=...) site
 *   j5.runwith=&lt;binary&gt;|&lt;value&gt;       an @org.junit.runner.RunWith value
 *   j5.extendwith=&lt;binary&gt;|&lt;value&gt;    an @org.junit.jupiter.api.extension.ExtendWith value
 *   j5.calls=&lt;binary&gt;|jupiter=&lt;n&gt;,jupiter.msg=&lt;n&gt;,legacy=&lt;n&gt;,legacy.msg=&lt;n&gt;,throws=&lt;n&gt;
 *                                     per-class call tallies; the five summary
 *                                     counters below are their column sums
 *   j5.classes=&lt;n&gt;                    classes read
 *   j5.seen.count=&lt;n&gt;                 distinct class names among them
 *   j5.tests=&lt;n&gt;
 *   j5.tests.junit3|junit4|junit5|testng=&lt;n&gt;
 *   j5.legacytests=&lt;n&gt;
 *   j5.expected.sites=&lt;n&gt;
 *   j5.call.jupiter=&lt;n&gt;               Assertions.* call sites
 *   j5.call.jupiter.msg=&lt;n&gt;           ... carrying a message
 *   j5.call.jupiter.throws=&lt;n&gt;        Assertions.assertThrows call sites
 *   j5.call.legacy=&lt;n&gt;                org.junit.Assert / junit.framework.* call sites
 *   j5.call.legacy.msg=&lt;n&gt;            ... carrying a message
 *   j5.call.testng=&lt;n&gt;                org.testng.Assert call sites
 * </pre>
 */
public final class Junit5
{
   // ------------------------------------------------------------ vocabulary

   /** Method-level and class-level markers, by framework. */
   private static final String[] JUNIT4_ANNOS = {
      "Lorg/junit/Test;", "Lorg/junit/Before;", "Lorg/junit/After;",
      "Lorg/junit/BeforeClass;", "Lorg/junit/AfterClass;", "Lorg/junit/Ignore;",
      "Lorg/junit/runner/RunWith;", "Lorg/junit/ClassRule;", "Lorg/junit/Rule;",
   };

   private static final String[] JUNIT5_ANNOS = {
      "Lorg/junit/jupiter/api/Test;", "Lorg/junit/jupiter/api/BeforeEach;",
      "Lorg/junit/jupiter/api/AfterEach;", "Lorg/junit/jupiter/api/BeforeAll;",
      "Lorg/junit/jupiter/api/AfterAll;", "Lorg/junit/jupiter/api/Disabled;",
      "Lorg/junit/jupiter/api/Nested;", "Lorg/junit/jupiter/api/TestFactory;",
      "Lorg/junit/jupiter/api/RepeatedTest;",
      "Lorg/junit/jupiter/params/ParameterizedTest;",
      "Lorg/junit/jupiter/api/extension/ExtendWith;",
   };

   private static final String[] TESTNG_ANNOS = {
      "Lorg/testng/annotations/Test;", "Lorg/testng/annotations/BeforeMethod;",
      "Lorg/testng/annotations/AfterMethod;", "Lorg/testng/annotations/BeforeClass;",
      "Lorg/testng/annotations/AfterClass;", "Lorg/testng/annotations/DataProvider;",
   };

   private static final String JUNIT3_TESTCASE = "junit/framework/TestCase";
   private static final String JUNIT4_TEST = "Lorg/junit/Test;";
   private static final String RUNWITH = "Lorg/junit/runner/RunWith;";
   private static final String EXTENDWITH = "Lorg/junit/jupiter/api/extension/ExtendWith;";

   private static final String JUPITER_ASSERTIONS = "org/junit/jupiter/api/Assertions";
   private static final String JUPITER_ASSUMPTIONS = "org/junit/jupiter/api/Assumptions";

   /**
    * Namespaces that step 2 retires from the suite.
    *
    * <p>{@code org/junit/jupiter}, {@code org/junit/platform}, {@code org/junit/support}
    * and {@code org/junit/vintage} all live under {@code org/junit} and are the
    * new world, not the old one; the prefix test has to exclude them explicitly
    * or every migrated class reads as unmigrated.
    */
   private static boolean legacy(String internalName)
   {
      if (internalName == null)
      {
         return false;
      }
      if (internalName.startsWith("junit/framework/") || internalName.startsWith("junit/textui/"))
      {
         return true;
      }
      if (!internalName.startsWith("org/junit/"))
      {
         return false;
      }
      return !internalName.startsWith("org/junit/jupiter/")
         && !internalName.startsWith("org/junit/platform/")
         && !internalName.startsWith("org/junit/support/")
         && !internalName.startsWith("org/junit/vintage/");
   }

   private static boolean legacyAssertOwner(String owner)
   {
      return "org/junit/Assert".equals(owner)
         || "junit/framework/Assert".equals(owner)
         || "junit/framework/TestCase".equals(owner);
   }

   // ----------------------------------------------------------------- state

   private int classes;
   /**
    * Every class name this run read, whether or not it said anything else.
    *
    * <p>Without it the report is a record of findings, and a scorer built on a
    * record of findings cannot tell "this class no longer names a retired JUnit
    * type" from "this class is not here any more". Deleting the test sources
    * answers every departure question perfectly and answers none of them
    * honestly; the fix is for presence to be a measurement like the others,
    * which is what this set is.
    */
   private final TreeSet<String> seen = new TreeSet<String>();
   /**
    * "&lt;binary&gt;|&lt;major&gt;" for every class file read. Per class file and not per
    * top-level class, because that is the unit a compiler emits and the unit
    * the question is about: a test class rewritten to Jupiter with an anonymous
    * Executable produces a second class file, and a build that raised its
    * source level to make that compile raises it for both.
    */
   private final TreeSet<String> majors = new TreeSet<String>();
   private final TreeMap<String, String> tests = new TreeMap<String, String>();
   private final TreeMap<String, String> asserts = new TreeMap<String, String>();
   private final TreeSet<String> legacyrefs = new TreeSet<String>();
   private final TreeSet<String> legacyRefClasses = new TreeSet<String>();
   private final TreeSet<String> expected = new TreeSet<String>();
   private final TreeSet<String> runwith = new TreeSet<String>();
   private final TreeSet<String> extendwith = new TreeSet<String>();
   /**
    * owner -> {jupiter, jupiter.msg, legacy, legacy.msg, throws}, for the
    * classes that call any.
    *
    * <p>The fifth slot is here because a total cannot answer a question about
    * placement. "Nineteen @Test(expected=) sites became nineteen assertThrows
    * calls" and "nineteen assertThrows calls appeared somewhere" are different
    * claims, and only the second is checkable from j5.call.jupiter.throws.
    */
   private final TreeMap<String, int[]> perClass = new TreeMap<String, int[]>();
   private int callJupiter;
   private int callJupiterMsg;
   private int callJupiterThrows;
   private int callLegacy;
   private int callLegacyMsg;
   private int callTestng;

   public static void main(String[] args) throws Exception
   {
      if (args.length < 2)
      {
         System.err.println("usage: Junit5 <out.txt> <jar-or-dir>...");
         System.exit(2);
      }
      List<File> roots = new ArrayList<File>();
      for (int i = 1; i < args.length; i++)
      {
         roots.add(new File(args[i]));
      }
      Collections.sort(roots);

      Junit5 j5 = new Junit5();
      for (File root : roots)
      {
         j5.walk(root);
      }

      PrintStream out = new PrintStream(new File(args[0]), "UTF-8");
      try
      {
         j5.report(out);
      }
      finally
      {
         out.close();
      }
      System.out.println("arqgate: classes=" + j5.classes
         + " tests=" + j5.tests.size()
         + " legacytests=" + j5.legacyTests().size()
         + " jupiter-calls=" + j5.callJupiter + "(msg=" + j5.callJupiterMsg + ")"
         + " legacy-calls=" + j5.callLegacy + "(msg=" + j5.callLegacyMsg + ")");
   }

   /**
    * A class that both looks like a test and still names a retired type.
    *
    * <p>The union of the two nets, not the intersection of the two lists: a
    * shared base class that carries no annotation but calls
    * {@code org.junit.Assert} is as unmigrated as one that carries
    * {@code @org.junit.Test}, and reporting only the annotated ones would leave
    * the most likely half-migration invisible. Whether any given class is
    * *allowed* to be on this list is the verifier's decision, not this tool's.
    */
   private TreeSet<String> legacyTests()
   {
      TreeSet<String> out = new TreeSet<String>();
      for (String c : legacyRefClasses)
      {
         if (tests.containsKey(c) || asserts.containsKey(c))
         {
            out.add(c);
         }
      }
      return out;
   }

   private void report(PrintStream out)
   {
      for (String c : seen)
      {
         out.println("j5.seen=" + c);
      }
      for (String c : majors)
      {
         out.println("j5.major=" + c);
      }
      for (java.util.Map.Entry<String, String> e : tests.entrySet())
      {
         out.println("j5.test=" + e.getKey() + "|" + e.getValue());
      }
      for (java.util.Map.Entry<String, String> e : asserts.entrySet())
      {
         out.println("j5.asserts=" + e.getKey() + "|" + e.getValue());
      }
      for (String r : legacyrefs)
      {
         out.println("j5.legacyref=" + r);
      }
      TreeSet<String> lt = legacyTests();
      for (String c : lt)
      {
         out.println("j5.legacytest=" + c);
      }
      for (String e : expected)
      {
         out.println("j5.expected=" + e);
      }
      for (String r : runwith)
      {
         out.println("j5.runwith=" + r);
      }
      for (String e : extendwith)
      {
         out.println("j5.extendwith=" + e);
      }
      for (java.util.Map.Entry<String, int[]> e : perClass.entrySet())
      {
         int[] n = e.getValue();
         out.println("j5.calls=" + e.getKey()
            + "|jupiter=" + n[0] + ",jupiter.msg=" + n[1]
            + ",legacy=" + n[2] + ",legacy.msg=" + n[3]
            + ",throws=" + n[4]);
      }
      out.println("j5.classes=" + classes);
      out.println("j5.seen.count=" + seen.size());
      out.println("j5.tests=" + tests.size());
      out.println("j5.tests.junit3=" + countFramework("junit3"));
      out.println("j5.tests.junit4=" + countFramework("junit4"));
      out.println("j5.tests.junit5=" + countFramework("junit5"));
      out.println("j5.tests.testng=" + countFramework("testng"));
      out.println("j5.legacytests=" + lt.size());
      out.println("j5.legacyrefs=" + legacyrefs.size());
      out.println("j5.expected.sites=" + expected.size());
      out.println("j5.call.jupiter=" + callJupiter);
      out.println("j5.call.jupiter.msg=" + callJupiterMsg);
      out.println("j5.call.jupiter.throws=" + callJupiterThrows);
      out.println("j5.call.legacy=" + callLegacy);
      out.println("j5.call.legacy.msg=" + callLegacyMsg);
      out.println("j5.call.testng=" + callTestng);
   }

   /**
    * Per-class call tallies, so a verifier can name the class that lost its
    * assertion messages instead of only reporting that the total fell.
    */
   private void tally(String owner, int slot)
   {
      int[] n = perClass.get(owner);
      if (n == null)
      {
         n = new int[5];
         perClass.put(owner, n);
      }
      n[slot]++;
   }

   private int countFramework(String fw)
   {
      int n = 0;
      for (String v : tests.values())
      {
         for (String part : v.split(","))
         {
            if (part.equals(fw))
            {
               n++;
               break;
            }
         }
      }
      return n;
   }

   // ------------------------------------------------------------------ walk

   private void walk(File root) throws IOException
   {
      if (root.isDirectory())
      {
         File[] kids = root.listFiles();
         if (kids == null)
         {
            return;
         }
         Arrays.sort(kids);
         for (File kid : kids)
         {
            walk(kid);
         }
         return;
      }
      String name = root.getName();
      if (name.endsWith(".class"))
      {
         visit(read(new FileInputStream(root)));
      }
      else if (name.endsWith(".jar") || name.endsWith(".zip"))
      {
         ZipFile zip = new ZipFile(root);
         try
         {
            List<String> entries = new ArrayList<String>();
            for (java.util.Enumeration<? extends ZipEntry> e = zip.entries(); e.hasMoreElements(); )
            {
               ZipEntry entry = e.nextElement();
               if (entry.getName().endsWith(".class"))
               {
                  entries.add(entry.getName());
               }
            }
            Collections.sort(entries);
            for (String entry : entries)
            {
               visit(read(zip.getInputStream(zip.getEntry(entry))));
            }
         }
         finally
         {
            zip.close();
         }
      }
   }

   private static byte[] read(InputStream in) throws IOException
   {
      try
      {
         ByteArrayOutputStream bos = new ByteArrayOutputStream();
         byte[] buf = new byte[8192];
         int n;
         while ((n = in.read(buf)) > 0)
         {
            bos.write(buf, 0, n);
         }
         return bos.toByteArray();
      }
      finally
      {
         in.close();
      }
   }

   private void visit(final byte[] bytes)
   {
      ClassReader cr;
      try
      {
         cr = new ClassReader(bytes);
      }
      catch (RuntimeException ignored)
      {
         // Not a class file this ASM can read. Counted nowhere rather than
         // counted as clean: Scan reports the same situation the same way.
         return;
      }
      classes++;

      final String owner = cr.getClassName();
      seen.add(owner);
      // Bytes 6-7 of every class file are the major version, read the same way
      // arqgate.Scan reads it -- from the header, not from a visitor callback,
      // so the two gates cannot disagree about what a class was compiled to.
      majors.add(owner + "|" + (((bytes[6] & 0xff) << 8) | (bytes[7] & 0xff)));
      final TreeSet<String> fw = new TreeSet<String>();
      final TreeSet<String> assertFamilies = new TreeSet<String>();

      // Structural references, read from the constant pool rather than from the
      // instruction stream: an annotation's element value, a Signature
      // attribute and a Class constant all name a type that no INVOKE names.
      for (String t : poolTypes(cr, bytes))
      {
         if (legacy(t))
         {
            legacyrefs.add(owner + "|" + t);
            legacyRefClasses.add(owner);
         }
      }

      ClassVisitor cv = new ClassVisitor(Opcodes.ASM9)
      {
         @Override
         public void visit(int version, int access, String name, String signature,
            String superName, String[] interfaces)
         {
            if (JUNIT3_TESTCASE.equals(superName))
            {
               fw.add("junit3");
            }
         }

         @Override
         public AnnotationVisitor visitAnnotation(String desc, boolean visible)
         {
            note(fw, desc);
            if (RUNWITH.equals(desc))
            {
               return collector(runwith, owner);
            }
            if (EXTENDWITH.equals(desc))
            {
               return collector(extendwith, owner);
            }
            return null;
         }

         @Override
         public MethodVisitor visitMethod(int access, final String mname, String mdesc,
            String msignature, String[] exceptions)
         {
            return new MethodVisitor(Opcodes.ASM9)
            {
               @Override
               public AnnotationVisitor visitAnnotation(String desc, boolean visible)
               {
                  note(fw, desc);
                  if (JUNIT4_TEST.equals(desc))
                  {
                     // JUnit 4's @Test has expected = Test.None.class by
                     // default and javac emits the element only when it was
                     // written, so its presence is the site.
                     return new AnnotationVisitor(Opcodes.ASM9)
                     {
                        @Override
                        public void visit(String name, Object value)
                        {
                           if ("expected".equals(name))
                           {
                              expected.add(owner + "#" + mname);
                           }
                        }
                     };
                  }
                  return null;
               }

               @Override
               public void visitMethodInsn(int opcode, String iowner, String iname,
                  String idesc, boolean itf)
               {
                  if (JUPITER_ASSERTIONS.equals(iowner) || JUPITER_ASSUMPTIONS.equals(iowner))
                  {
                     assertFamilies.add("jupiter");
                     callJupiter++;
                     tally(owner, 0);
                     if ("assertThrows".equals(iname) || "assertThrowsExactly".equals(iname))
                     {
                        callJupiterThrows++;
                        tally(owner, 4);
                     }
                     else if (jupiterCarriesMessage(iname, idesc))
                     {
                        callJupiterMsg++;
                        tally(owner, 1);
                     }
                  }
                  else if (legacyAssertOwner(iowner) && legacyAssertVerb(iname))
                  {
                     assertFamilies.add("legacy");
                     callLegacy++;
                     tally(owner, 2);
                     if (legacyCarriesMessage(idesc))
                     {
                        callLegacyMsg++;
                        tally(owner, 3);
                     }
                  }
                  else if ("org/testng/Assert".equals(iowner))
                  {
                     assertFamilies.add("testng");
                     callTestng++;
                  }
               }
            };
         }
      };
      cr.accept(cv, ClassReader.SKIP_FRAMES);

      if (!fw.isEmpty())
      {
         tests.put(owner, join(fw));
      }
      if (!assertFamilies.isEmpty())
      {
         asserts.put(owner, join(assertFamilies));
      }
   }

   /**
    * JUnit 5 puts the failure message last, as a {@code String} or as a
    * {@code Supplier<String>}.
    *
    * <p>{@code fail(String)} is excluded by the caller's shape rather than here:
    * it is a one-argument method whose only argument *is* the message, so
    * counting it would inflate the "messages were preserved" number with calls
    * that never had a subject to lose.
    */
   /**
    * The verbs {@code Assertions} and {@code Assumptions} expose, named on the
    * legacy side so the two counters measure the same thing. Restricting the
    * legacy side to {@code assert*} while the Jupiter side took every method on
    * the class made {@code fail(...)} count once after migration and never
    * before it, and six sites in this tree do exactly that: a census built on
    * the asymmetric pair reports six calls appearing out of nowhere.
    */
   private static boolean legacyAssertVerb(String name)
   {
      return name.startsWith("assert") || name.startsWith("fail") || name.startsWith("assume");
   }

   private static boolean jupiterCarriesMessage(String name, String desc)
   {
      if (name.startsWith("fail"))
      {
         return false;
      }
      Type[] args = Type.getArgumentTypes(desc);
      if (args.length < 2)
      {
         return false;
      }
      String last = args[args.length - 1].getDescriptor();
      return "Ljava/lang/String;".equals(last) || "Ljava/util/function/Supplier;".equals(last);
   }

   /**
    * JUnit 4 and JUnit 3 put it first.
    *
    * <p>Two arguments minimum for the same reason: {@code fail(String)} and
    * {@code assertEquals(String,String)} -- the latter binds to
    * {@code (Object,Object)} in JUnit 4 and so never reaches this shape -- are
    * not messages being carried.
    */
   /**
    * JUnit 3/4 put the message first, so "first argument is a String" is almost
    * the whole rule -- but not quite, and the exception was measured rather than
    * reasoned about. {@code junit.framework.Assert} carries an
    * {@code assertEquals(String expected, String actual)} overload: two Strings,
    * no message, and the naive rule counts it as one. Four sites in this tree
    * use it, and they are why a step-1-to-step-2 message census did not balance
    * until this method stopped looking only at {@code args[0]}.
    *
    * <p>Two arguments that are both String is unambiguous: no JUnit 3/4
    * assertion takes a message plus a single String subject, so that shape is
    * always expected/actual. Every other String-first shape -- (String,Object),
    * (String,boolean), (String,X,X) -- is a message.
    */
   private static boolean legacyCarriesMessage(String desc)
   {
      Type[] args = Type.getArgumentTypes(desc);
      if (args.length < 2 || !"Ljava/lang/String;".equals(args[0].getDescriptor()))
      {
         return false;
      }
      return !(args.length == 2 && "Ljava/lang/String;".equals(args[1].getDescriptor()));
   }

   private static void note(TreeSet<String> fw, String desc)
   {
      for (String a : JUNIT4_ANNOS)
      {
         if (a.equals(desc))
         {
            fw.add("junit4");
            return;
         }
      }
      for (String a : JUNIT5_ANNOS)
      {
         if (a.equals(desc))
         {
            fw.add("junit5");
            return;
         }
      }
      for (String a : TESTNG_ANNOS)
      {
         if (a.equals(desc))
         {
            fw.add("testng");
            return;
         }
      }
   }

   /**
    * Collect a class-valued annotation element, whether it was written as one
    * value or as an array of them. {@code @ExtendWith(MockitoExtension.class)}
    * and {@code @ExtendWith({A.class, B.class})} are the same annotation with
    * the same element, and only the second one arrives through visitArray.
    */
   private static AnnotationVisitor collector(final TreeSet<String> into, final String owner)
   {
      return new AnnotationVisitor(Opcodes.ASM9)
      {
         @Override
         public void visit(String name, Object value)
         {
            if (value instanceof Type)
            {
               into.add(owner + "|" + ((Type) value).getInternalName());
            }
         }

         @Override
         public AnnotationVisitor visitArray(String name)
         {
            return this;
         }
      };
   }

   private static String join(TreeSet<String> set)
   {
      StringBuilder sb = new StringBuilder();
      for (String s : set)
      {
         if (sb.length() > 0)
         {
            sb.append(',');
         }
         sb.append(s);
      }
      return sb.toString();
   }

   // --------------------------------------------------------- constant pool
   //
   // The same walk Scan does, with the namespace test moved out to the caller.
   // Duplicated rather than shared because Scan's copy is in eleven steps'
   // shipped payloads and already reviewed; a refactor that touched it would
   // change bytes in every one of them to save twenty lines here.

   private static TreeSet<String> poolTypes(ClassReader cr, byte[] bytes)
   {
      TreeSet<String> types = new TreeSet<String>();
      char[] buf = new char[cr.getMaxStringLength()];
      int items = cr.getItemCount();
      for (int i = 1; i < items; i++)
      {
         int off = cr.getItem(i);
         if (off == 0)
         {
            continue; // the unused slot that follows a long or a double
         }
         int tag = bytes[off - 1] & 0xff;
         if (tag == 7) // CONSTANT_Class
         {
            String text = cr.readUTF8(off, buf);
            String el = element(text);
            if (el != null)
            {
               types.add(el);
            }
         }
         else if (tag == 1) // CONSTANT_Utf8
         {
            String text = utf8(bytes, off);
            if (text != null)
            {
               harvestDescriptor(types, text);
            }
         }
      }
      return types;
   }

   private static String element(String internalOrDescriptor)
   {
      if (internalOrDescriptor == null)
      {
         return null;
      }
      String s = internalOrDescriptor;
      int i = 0;
      while (i < s.length() && s.charAt(i) == '[')
      {
         i++;
      }
      if (i == 0)
      {
         return s;
      }
      if (i < s.length() && s.charAt(i) == 'L' && s.endsWith(";"))
      {
         return s.substring(i + 1, s.length() - 1);
      }
      return null; // an array of a primitive
   }

   private static void harvestDescriptor(TreeSet<String> into, String text)
   {
      int from = 0;
      while (true)
      {
         int l = text.indexOf('L', from);
         if (l < 0)
         {
            return;
         }
         int end = l + 1;
         while (end < text.length())
         {
            char c = text.charAt(end);
            if (c == ';' || c == '<')
            {
               break;
            }
            if (c != '/' && c != '$' && !Character.isJavaIdentifierPart(c))
            {
               break;
            }
            end++;
         }
         if (end < text.length() && (text.charAt(end) == ';' || text.charAt(end) == '<')
            && end > l + 1)
         {
            into.add(text.substring(l + 1, end));
         }
         from = l + 1;
      }
   }

   /** Decode one CONSTANT_Utf8 entry. Modified UTF-8, so not new String(...). */
   private static String utf8(byte[] bytes, int off)
   {
      int len = ((bytes[off] & 0xff) << 8) | (bytes[off + 1] & 0xff);
      StringBuilder sb = new StringBuilder(len);
      int i = off + 2;
      int end = i + len;
      if (end > bytes.length)
      {
         return null;
      }
      while (i < end)
      {
         int b = bytes[i++] & 0xff;
         if (b < 0x80)
         {
            sb.append((char) b);
         }
         else if ((b & 0xe0) == 0xc0)
         {
            if (i >= end)
            {
               return null;
            }
            sb.append((char) (((b & 0x1f) << 6) | (bytes[i++] & 0x3f)));
         }
         else if ((b & 0xf0) == 0xe0)
         {
            if (i + 1 >= end)
            {
               return null;
            }
            int b2 = bytes[i++] & 0x3f;
            int b3 = bytes[i++] & 0x3f;
            sb.append((char) (((b & 0x0f) << 12) | (b2 << 6) | b3));
         }
         else
         {
            return null;
         }
      }
      return sb.toString();
   }

   private Junit5()
   {
   }
}
