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
 * The test-population half of the migrated-ness gates: what tests does the tree
 * the candidate built actually contain?
 *
 * <pre>
 *   java -cp arqgate.jar:asm.jar arqgate.Tests &lt;out.txt&gt; &lt;jar-or-dir&gt;...
 * </pre>
 *
 * <p><b>Why not read the test reports.</b> The obvious instrument is the XML the
 * build's test task writes, and it is the wrong one. Measured on the pinned
 * baseline: Maven's surefire reports sum to <em>159</em> across their suite
 * attributes, print <em>157</em> in the build log, and contain <em>158</em>
 * distinct {@code class#method} pairs -- because the TestNG module's reporter
 * writes each case into both {@code TEST-TestSuite.xml} and a per-class file,
 * and records one expected-exception case as {@code <error>} in the per-class
 * copy while the suite attributes call it a pass. Three numbers, one run, and
 * none of them is wrong; they answer slightly different questions. Layer a
 * second build system on top -- which is exactly what this ladder's first step
 * does -- and the file names, the directory and the double-write all change
 * again.
 *
 * <p>The class files do not. A test method is a method with a test annotation on
 * it, and that is true whoever compiled it and whoever ran it. So this reads the
 * artifacts.
 *
 * <p><b>It measures, and never decides.</b> Like {@link Scan}, there is no
 * threshold here and no expectation. It recognizes the JUnit 3, JUnit 4, JUnit 5
 * and TestNG vocabularies all at once, so that one tool serves a ladder whose
 * steps sit in different eras, and the step's own verifier decides what the
 * numbers have to be.
 *
 * <p>Output, sorted, one fact per line:
 *
 * <pre>
 *   test.class=&lt;binary name&gt;
 *   test.method=&lt;binary name&gt;#&lt;method&gt;
 *   test.disabled=&lt;binary name&gt;#&lt;method&gt;
 *   tests.classes=&lt;n&gt;
 *   tests.methods=&lt;n&gt;
 *   tests.disabled=&lt;n&gt;
 *   tests.frameworks=&lt;junit3|junit4|junit5|testng, ...&gt;
 * </pre>
 *
 * <p>A disabled method is still reported as a method. It has to be: the whole
 * point of counting them separately is that turning a test off is a way of
 * making a suite pass without making the code work, and a tool that dropped
 * disabled methods from the population would make that invisible -- the same
 * reasoning rule 11 applies to suppressed linter findings.
 */
public class Tests extends ClassVisitor
{
   // JUnit 4 / 5 / TestNG method-level test markers.
   private static final String JUNIT4_TEST = "Lorg/junit/Test;";
   private static final String JUNIT5_TEST = "Lorg/junit/jupiter/api/Test;";
   private static final String JUNIT5_PARAMS = "Lorg/junit/jupiter/params/ParameterizedTest;";
   private static final String JUNIT5_FACTORY = "Lorg/junit/jupiter/api/TestFactory;";
   private static final String JUNIT5_REPEATED = "Lorg/junit/jupiter/api/RepeatedTest;";
   private static final String TESTNG_TEST = "Lorg/testng/annotations/Test;";

   // Disabling markers. TestNG has no annotation for it -- it is the
   // enabled=false element of @Test -- so that one is read from the annotation
   // body rather than from its presence.
   private static final String JUNIT4_IGNORE = "Lorg/junit/Ignore;";
   private static final String JUNIT5_DISABLED = "Lorg/junit/jupiter/api/Disabled;";

   // Direct subclasses only. A deeper JUnit 3 hierarchy would need the
   // supertype chain, which needs the classpath, which would make this tool
   // depend on resolving the tree it is measuring. The gate that uses this
   // number is a comparison against a recorded baseline, so a type this misses
   // is missed identically on both sides and cannot turn into a false verdict;
   // it would only ever under-report, and the recorded baseline says how many
   // that is (zero, on this tree -- tests.frameworks has no junit3 in it).
   private static final String JUNIT3_TESTCASE = "junit/framework/TestCase";

   private final TreeSet<String> classes = new TreeSet<String>();
   private final TreeSet<String> methods = new TreeSet<String>();
   private final TreeSet<String> disabled = new TreeSet<String>();
   private final TreeSet<String> frameworks = new TreeSet<String>();

   // Per-class state, reset in visit().
   private String internalName;
   private boolean junit3;
   private boolean classLevelTestNg;
   private boolean classLevelDisabled;
   private final List<String[]> pending = new ArrayList<String[]>();

   public Tests()
   {
      super(Opcodes.ASM9);
   }

   public static void main(String[] args) throws Exception
   {
      if (args.length < 2)
      {
         System.err.println("usage: Tests <out.txt> <jar-or-dir>...");
         System.exit(2);
      }
      List<File> roots = new ArrayList<File>();
      for (int i = 1; i < args.length; i++)
      {
         roots.add(new File(args[i]));
      }
      Collections.sort(roots);

      Tests tests = new Tests();
      for (File root : roots)
      {
         tests.walk(root);
      }

      PrintStream out = new PrintStream(new File(args[0]), "UTF-8");
      try
      {
         tests.report(out);
      }
      finally
      {
         out.close();
      }
      System.out.println("arqgate: test classes=" + tests.classes.size()
         + " methods=" + tests.methods.size()
         + " disabled=" + tests.disabled.size()
         + " frameworks=" + tests.frameworks);
   }

   private void report(PrintStream out)
   {
      for (String c : classes)
      {
         out.println("test.class=" + c);
      }
      for (String m : methods)
      {
         out.println("test.method=" + m);
      }
      for (String d : disabled)
      {
         out.println("test.disabled=" + d);
      }
      out.println("tests.classes=" + classes.size());
      out.println("tests.methods=" + methods.size());
      out.println("tests.disabled=" + disabled.size());
      StringBuilder fw = new StringBuilder();
      for (String f : frameworks)
      {
         if (fw.length() > 0)
         {
            fw.append(',');
         }
         fw.append(f);
      }
      out.println("tests.frameworks=" + fw);
   }

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
            // Sorted, so two runs over the same tree agree byte for byte
            // whatever order the packager wrote the entries in.
            Collections.sort(entries);
            for (String entryName : entries)
            {
               visit(read(zip.getInputStream(zip.getEntry(entryName))));
            }
         }
         finally
         {
            zip.close();
         }
      }
   }

   private void visit(byte[] bytes)
   {
      new ClassReader(bytes).accept(this, ClassReader.SKIP_FRAMES | ClassReader.SKIP_DEBUG);
   }

   private static byte[] read(InputStream in) throws IOException
   {
      try
      {
         ByteArrayOutputStream buf = new ByteArrayOutputStream();
         byte[] chunk = new byte[8192];
         int n;
         while ((n = in.read(chunk)) > 0)
         {
            buf.write(chunk, 0, n);
         }
         return buf.toByteArray();
      }
      finally
      {
         in.close();
      }
   }

   // ------------------------------------------------------------------ visitor

   @Override
   public void visit(int version, int access, String name, String signature,
      String superName, String[] interfaces)
   {
      flush();
      this.internalName = name;
      this.junit3 = JUNIT3_TESTCASE.equals(superName);
      this.classLevelTestNg = false;
      this.classLevelDisabled = false;
      if (junit3)
      {
         frameworks.add("junit3");
      }
   }

   @Override
   public AnnotationVisitor visitAnnotation(String descriptor, boolean visible)
   {
      if (TESTNG_TEST.equals(descriptor))
      {
         // A class-level TestNG @Test makes every public instance method a test.
         classLevelTestNg = true;
         frameworks.add("testng");
         return new EnabledReader(true);
      }
      if (JUNIT4_IGNORE.equals(descriptor) || JUNIT5_DISABLED.equals(descriptor))
      {
         classLevelDisabled = true;
      }
      return null;
   }

   @Override
   public MethodVisitor visitMethod(int access, String name, String descriptor,
      String signature, String[] exceptions)
   {
      if ((access & Opcodes.ACC_SYNTHETIC) != 0 || (access & Opcodes.ACC_BRIDGE) != 0)
      {
         return null;
      }
      if ("<init>".equals(name) || "<clinit>".equals(name))
      {
         return null;
      }
      return new MethodScan(access, name, descriptor);
   }

   /**
    * Reads {@code enabled=false} out of a TestNG {@code @Test} body. TestNG is
    * the one framework here that turns a test off with an annotation element
    * rather than with an annotation, so its disabled tests are invisible to a
    * scan that only looks at which annotations are present.
    */
   private final class EnabledReader extends AnnotationVisitor
   {
      private final boolean classLevel;
      private String owner;

      EnabledReader(boolean classLevel)
      {
         super(Opcodes.ASM9);
         this.classLevel = classLevel;
      }

      EnabledReader owner(String value)
      {
         this.owner = value;
         return this;
      }

      @Override
      public void visit(String name, Object value)
      {
         if ("enabled".equals(name) && Boolean.FALSE.equals(value))
         {
            if (classLevel)
            {
               classLevelDisabled = true;
            }
            else if (owner != null)
            {
               disabled.add(owner);
            }
         }
      }
   }

   private final class MethodScan extends MethodVisitor
   {
      private final int access;
      private final String name;
      private final String descriptor;
      private boolean isTest;
      private boolean isDisabled;

      MethodScan(int access, String name, String descriptor)
      {
         super(Opcodes.ASM9);
         this.access = access;
         this.name = name;
         this.descriptor = descriptor;
      }

      @Override
      public AnnotationVisitor visitAnnotation(String annotation, boolean visible)
      {
         if (JUNIT4_TEST.equals(annotation))
         {
            isTest = true;
            frameworks.add("junit4");
         }
         else if (JUNIT5_TEST.equals(annotation) || JUNIT5_PARAMS.equals(annotation)
            || JUNIT5_FACTORY.equals(annotation) || JUNIT5_REPEATED.equals(annotation))
         {
            isTest = true;
            frameworks.add("junit5");
         }
         else if (TESTNG_TEST.equals(annotation))
         {
            isTest = true;
            frameworks.add("testng");
            return new EnabledReader(false).owner(internalName.replace('/', '.') + "#" + name);
         }
         else if (JUNIT4_IGNORE.equals(annotation) || JUNIT5_DISABLED.equals(annotation))
         {
            isDisabled = true;
         }
         return null;
      }

      @Override
      public void visitEnd()
      {
         // JUnit 3: a public no-arg void method whose name starts with "test",
         // on a TestCase subclass. TestNG class-level @Test: every public
         // instance method, which is TestNG's own rule.
         boolean conventional =
            (access & Opcodes.ACC_PUBLIC) != 0
               && (access & Opcodes.ACC_STATIC) == 0
               && descriptor.startsWith("()")
               && Type.VOID_TYPE.equals(Type.getReturnType(descriptor));
         if (junit3 && conventional && name.startsWith("test"))
         {
            isTest = true;
         }
         if (classLevelTestNg && conventional)
         {
            isTest = true;
         }
         if (isTest)
         {
            pending.add(new String[] { name, isDisabled ? "off" : "on" });
         }
      }
   }

   /**
    * Per-class results are held until the class ends, because whether the class
    * belongs in {@code test.class=} at all is only known once every method has
    * been seen: a class with no test method is not a test class, however it is
    * named and whatever it extends.
    */
   private void flush()
   {
      if (internalName == null)
      {
         pending.clear();
         return;
      }
      String dotted = internalName.replace('/', '.');
      if (!pending.isEmpty())
      {
         classes.add(dotted);
      }
      for (String[] entry : pending)
      {
         String id = dotted + "#" + entry[0];
         methods.add(id);
         if (classLevelDisabled || "off".equals(entry[1]))
         {
            disabled.add(id);
         }
      }
      pending.clear();
   }

   @Override
   public void visitEnd()
   {
      flush();
      internalName = null;
   }
}
