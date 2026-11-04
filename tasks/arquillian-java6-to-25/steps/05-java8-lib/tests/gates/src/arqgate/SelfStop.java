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

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ConstantDynamic;
import org.objectweb.asm.FieldVisitor;
import org.objectweb.asm.Handle;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.Type;

/**
 * Does anything in this tree stop the JVM out from under a test runner?
 *
 * <pre>
 *   java -cp arqgate.jar:asm.jar arqgate.SelfStop &lt;out.txt&gt; &lt;dir-or-jar&gt;...
 * </pre>
 *
 * <p><b>Why this exists.</b> B-7 asks whether the inherited suite actually ran,
 * and it answers partly by re-running each frozen test class under JUnitCore and
 * comparing. Everything that runs in that JVM is the candidate's, so a forged
 * transcript cannot be excluded by reading the transcript harder -- code can
 * print whatever it likes, in whatever order. What it cannot do is outlive
 * JUnitCore's own verdict: {@code JUnitCore.runMain} prints its banner before it
 * loads anything named on the command line, prints the real result last, and
 * sets the exit status from it. So a forged pass only survives if the JVM stops
 * before that. This scanner looks for the stopping, not for the forgery.
 *
 * <p><b>Why a call-site scan and not a byte grep.</b> The first version of this
 * check was {@code grep -qaF 'java/lang/Runtime'} and {@code grep -qaF 'halt'}
 * over raw class bytes, and review round 4 was right about both of its edges:
 *
 * <ul>
 *   <li>too wide -- {@code java/lang/RuntimeException} <em>contains</em>
 *       {@code java/lang/Runtime}, and {@code exit} is a substring of any Utf8
 *       entry that happens to spell it, so an innocent class that throws and
 *       logs could be charged;</li>
 *   <li>too narrow -- it walked loose {@code .class} files, and the
 *       corroboration classpath also carries the candidate's twelve module
 *       jars. A helper packed into a jar, with the loose copy deleted, was
 *       invisible.</li>
 * </ul>
 *
 * <p>So: ASM, over the same artifacts corroboration loads, matching an actual
 * {@code invoke} of an actual method on an actual owner. A class that mentions
 * {@code RuntimeException} and the word "halt" is not a call and is not
 * reported.
 *
 * <p><b>What counts.</b> Five routes, because the question is "can this stop the
 * JVM", not "does it name a class":
 *
 * <ul>
 *   <li>{@code Runtime.halt}, {@code Runtime.exit}, {@code System.exit} --
 *       invoked, by owner and name;</li>
 *   <li>{@code sun.misc.Unsafe} / {@code jdk.internal.misc.Unsafe} -- invoked,
 *       referenced as a type, or read as a field type. {@code Unsafe} can stop
 *       a JVM several ways and none of them are things this project does;</li>
 *   <li>the reflective route: a class that names the target <em>and</em> calls
 *       the reflection machinery that would turn that name into an invocation.
 *       Both halves are required, because a name alone is not a call: the clean
 *       face rehearsed at image-build time holds {@code "java.lang.Runtime"}
 *       and {@code "exit"} in its constant pool -- it logs the one and
 *       concatenates the other -- and it must come back clean. The owner half
 *       is read three ways, for the same reason {@link Scan} reads the pool as
 *       both {@code strref} and {@code typeref}: as the dotted string constant
 *       {@code Class.forName} takes; as a reference to the <em>type</em>, which
 *       is what {@code Runtime.class} leaves behind; and as the owner of an
 *       invoke. A class literal is a {@code CONSTANT_Class} and never a
 *       {@code CONSTANT_String}, so a Utf8-only rule does not see
 *       {@code Runtime.class.getMethod("halt", int.class).invoke(...)}; and a
 *       rule that reads only constants does not see
 *       {@code Runtime.getRuntime().getClass().getMethod("halt", ...)}, where
 *       the class object comes from the factory and from
 *       {@code Object.getClass} and the only mention of the type in the whole
 *       class is the owner of {@code getRuntime}. Both are ordinary Java
 *       6-legal reflection and between them they are the cheapest spellings
 *       there are. None of the three readings is ever a hit on its own:
 *       {@code java.lang.System} is in the constant pool of most classes ever
 *       written, and a class that asks the runtime how many processors it has
 *       has named the type without doing anything with it;</li>
 *   <li>a {@code Handle} -- a method-handle constant -- whose owner is
 *       {@code Runtime} or {@code System} and whose name is {@code halt},
 *       {@code exit}, {@code load} or {@code loadLibrary}. That one is a hit on
 *       its own, with no pair needed: unlike a string, a method-handle constant
 *       is a resolved reference to exactly that method, and the only thing left
 *       to do with it is invoke it;</li>
 *   <li>{@code System.load} / {@code loadLibrary} -- a native library can call
 *       {@code _exit} and no Java-side scan would see it. Measured zero on the
 *       inherited tree, so it costs a correct migration nothing.</li>
 * </ul>
 *
 * <p><b>What it deliberately does not scan.</b> Jars found by walking a
 * directory. The roots are the tree (for loose class files) and the module jars
 * named explicitly by the caller, which is exactly the candidate's contribution
 * to the corroboration classpath. A tree can also contain a Gradle cache full of
 * third-party jars, and half of Maven Central calls {@code System.exit}
 * somewhere; charging a candidate for the contents of a downloaded dependency
 * would be charging them for the ecosystem.
 *
 * <p><b>Unreadable classes.</b> Counted and named, never fatal, and never a hit.
 * A truncated class file is a tree that will fail to load that class under
 * JUnitCore anyway, where it is charged by a gate that knows what it is looking
 * at. A scanner that dies on one malformed file is a scanner a hostile tree can
 * switch off.
 *
 * <p>ASCII-only, Java 8 source level: this jar is built by the JDK 8 javac in
 * the step 1 and 2 verifier images.
 */
public final class SelfStop
{
   /** Internal names whose mere presence is a route to stopping the JVM. */
   private static final String[] UNSAFE = {
      "sun/misc/Unsafe", "jdk/internal/misc/Unsafe"
   };

   /**
    * The two owners a reflective stop has to name. Named as a <em>type</em>
    * these are the owner half of the reflective pair and never a hit on their
    * own -- see the class comment.
    */
   private static final String[] STOP_OWNERS = {
      "java/lang/Runtime", "java/lang/System"
   };

   public static void main(String[] args) throws IOException
   {
      if (args.length < 2)
      {
         System.err.println("usage: arqgate.SelfStop <out.txt> <dir-or-jar>...");
         System.exit(2);
      }
      List<File> roots = new ArrayList<File>();
      for (int i = 1; i < args.length; i++)
      {
         roots.add(new File(args[i]));
      }
      Collections.sort(roots);

      SelfStop s = new SelfStop();
      for (File root : roots)
      {
         // Per root, because a root is a thing the candidate supplied. A jar
         // that will not open, or a directory that vanishes mid-walk, must cost
         // that root and not the other eleven -- a scanner that a single
         // hostile artifact can switch off entirely is not a gate.
         try
         {
            s.walk(root, true);
         }
         catch (Throwable t)
         {
            s.unreadable.add(root.getPath() + " (" + t.getClass().getName() + ")");
         }
      }

      PrintStream out = new PrintStream(new File(args[0]), "UTF-8");
      try
      {
         for (String hit : s.hits)
         {
            out.println(hit);
         }
      }
      finally
      {
         out.close();
      }
      PrintStream note = new PrintStream(new File(args[0] + ".unreadable"), "UTF-8");
      try
      {
         for (String u : s.unreadable)
         {
            note.println(u);
         }
      }
      finally
      {
         note.close();
      }
      System.out.println("arqgate.selfstop: roots=" + roots.size() + " classes=" + s.classes
         + " hits=" + s.hits.size() + " unreadable=" + s.unreadable.size());
   }

   // ---------------------------------------------------------------- state

   private int classes;
   /** Sorted so two runs over the same tree produce the same file. */
   private final TreeSet<String> hits = new TreeSet<String>();
   private final TreeSet<String> unreadable = new TreeSet<String>();

   // ----------------------------------------------------------------- walk

   /**
    * @param topLevel whether a jar here was named by the caller. Jars found by
    *                 walking a directory are skipped -- see the class comment.
    */
   private void walk(File root, boolean topLevel) throws IOException
   {
      if (root.isDirectory())
      {
         File[] kids = root.listFiles();
         if (kids == null)
         {
            // A directory that will not list is a part of the tree this scan
            // did not see. Silence here is the one way to stop the walk without
            // throwing, so it is recorded rather than returned from.
            unreadable.add(root.getPath() + " (unlistable)");
            return;
         }
         Arrays.sort(kids);
         for (File kid : kids)
         {
            walk(kid, false);
         }
         return;
      }
      String name = root.getName();
      if (name.endsWith(".class"))
      {
         byte[] b = read(new FileInputStream(root));
         if (b != null)
         {
            visit(b, root.getPath());
         }
         else
         {
            unreadable.add(root.getPath());
         }
      }
      else if (topLevel && (name.endsWith(".jar") || name.endsWith(".zip")))
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
               String where = root.getPath() + "!" + entry;
               byte[] b = null;
               try
               {
                  ZipEntry ze = zip.getEntry(entry);
                  if (ze != null)
                  {
                     b = read(zip.getInputStream(ze));
                  }
               }
               catch (Throwable t)
               {
                  b = null;
               }
               if (b != null)
               {
                  visit(b, where);
               }
               else
               {
                  unreadable.add(where);
               }
            }
         }
         finally
         {
            zip.close();
         }
      }
   }

   private static byte[] read(InputStream in)
   {
      try
      {
         ByteArrayOutputStream bout = new ByteArrayOutputStream();
         byte[] buf = new byte[8192];
         int n;
         while ((n = in.read(buf)) > 0)
         {
            bout.write(buf, 0, n);
         }
         return bout.toByteArray();
      }
      catch (Throwable t)
      {
         return null;
      }
      finally
      {
         try
         {
            in.close();
         }
         catch (IOException ignored)
         {
            // nothing to do about it, and not a finding
         }
      }
   }

   // ---------------------------------------------------------------- visit

   private void visit(byte[] bytes, String where)
   {
      Finder finder = new Finder(where);
      try
      {
         new ClassReader(bytes).accept(finder, 0);
         classes++;
      }
      catch (Throwable t)
      {
         // Malformed, truncated, or a class file version ASM does not know.
         // Named, not fatal, and not a hit.
         unreadable.add(where + " (" + t.getClass().getName() + ")");
         return;
      }
      finder.finish(hits);
   }

   private static boolean isUnsafe(String internalName)
   {
      if (internalName == null)
      {
         return false;
      }
      for (String u : UNSAFE)
      {
         if (u.equals(internalName))
         {
            return true;
         }
      }
      return false;
   }

   private static boolean isStopOwner(String internalName)
   {
      if (internalName == null)
      {
         return false;
      }
      for (String o : STOP_OWNERS)
      {
         if (o.equals(internalName))
         {
            return true;
         }
      }
      return false;
   }

   /**
    * The machinery that turns a name into an invocation: lookup, then call.
    * Field access is in here because that is how {@code theUnsafe} is taken.
    */
   private static boolean isReflection(String owner, String name)
   {
      if ("java/lang/Class".equals(owner))
      {
         return "forName".equals(name) || "getMethod".equals(name)
            || "getDeclaredMethod".equals(name) || "getField".equals(name)
            || "getDeclaredField".equals(name);
      }
      if ("java/lang/reflect/Method".equals(owner) || "java/lang/reflect/Field".equals(owner))
      {
         return "invoke".equals(name) || "get".equals(name) || "setAccessible".equals(name);
      }
      if ("java/lang/invoke/MethodHandles$Lookup".equals(owner))
      {
         return name.startsWith("find") || "unreflect".equals(name);
      }
      return false;
   }

   /** {@code Lsun/misc/Unsafe;} and {@code [Lsun/misc/Unsafe;} both count. */
   private static boolean isUnsafeDesc(String desc)
   {
      if (desc == null || desc.indexOf('L') < 0)
      {
         return false;
      }
      try
      {
         Type t = Type.getType(desc);
         while (t.getSort() == Type.ARRAY)
         {
            t = t.getElementType();
         }
         return t.getSort() == Type.OBJECT && isUnsafe(t.getInternalName());
      }
      catch (Throwable ignored)
      {
         return false;
      }
   }

   /**
    * One class. Call sites are reported as they are found; the reflective route
    * needs the whole class read first, so it is settled in {@link #finish}.
    */
   private static final class Finder extends ClassVisitor
   {
      private final String where;
      private final TreeSet<String> found = new TreeSet<String>();
      private final TreeSet<String> strings = new TreeSet<String>();
      /**
       * {@code java/lang/Runtime} / {@code java/lang/System} where the class
       * names the <em>type</em>: a {@code Runtime.class} literal, a cast, a
       * method-handle owner, an {@code invokedynamic} bootstrap argument. The
       * owner half of the pair; never a hit by itself.
       */
      private final TreeSet<String> stopTypes = new TreeSet<String>();
      /**
       * {@code java/lang/Runtime} where the class <em>calls</em> something on
       * it. The third reading of the owner half, and the one that sees
       * {@code Runtime.getRuntime().getClass()}: a factory call names the type
       * as surely as a class literal does, and {@code Object.getClass} on its
       * result leaves nothing else behind.
       *
       * <p>{@code java/lang/System} is deliberately not collected here. It is
       * not a narrowing for convenience: {@code System} is never instantiated,
       * so there is no {@code getClass()} on a {@code System} value and no
       * factory that hands one back. Every route to that class object is
       * already half of the pair -- {@code System.class} is a {@code Type}
       * constant and {@code Class.forName("java.lang.System")} is a Utf8 --
       * so collecting it here would close nothing, while
       * {@code System.getProperty}, {@code System.arraycopy} and
       * {@code System.currentTimeMillis} are called by ordinary code
       * everywhere and would put the owner half within reach of any class that
       * also reflects and happens to hold the word "exit".
       */
      private final TreeSet<String> callOwners = new TreeSet<String>();
      private String owner = "?";
      /** Does this class call anything that turns a name into an invocation? */
      private boolean reflective;

      Finder(String where)
      {
         super(Opcodes.ASM9);
         this.where = where;
      }

      /**
       * One type reference. Arrays are unwrapped to their element type and a
       * {@code MethodType} constant to its argument and return types, because
       * {@code Runtime[].class} and
       * {@code MethodType.methodType(void.class, Runtime.class)} name the owner
       * as surely as a bare class literal does.
       */
      private void noteType(Type t)
      {
         if (t == null)
         {
            return;
         }
         if (t.getSort() == Type.METHOD)
         {
            noteType(t.getReturnType());
            for (Type arg : t.getArgumentTypes())
            {
               noteType(arg);
            }
            return;
         }
         while (t.getSort() == Type.ARRAY)
         {
            t = t.getElementType();
         }
         if (t.getSort() != Type.OBJECT)
         {
            return;
         }
         if (isUnsafe(t.getInternalName()))
         {
            found.add("Unsafe");
         }
         else if (isStopOwner(t.getInternalName()))
         {
            stopTypes.add(t.getInternalName());
         }
      }

      /**
       * One method-handle constant. Reported on its own, with no pair needed:
       * a {@code Handle} is a resolved reference to exactly one method on
       * exactly one owner, so it is not ambiguous the way a string is. Its
       * owner also counts as the owner half for the pair rule, since
       * {@code findVirtual(Runtime.class, ...)} and the handle it returns are
       * the same construction seen at two moments.
       */
      private void noteHandle(Handle h)
      {
         if (h == null)
         {
            return;
         }
         String o = h.getOwner();
         String n = h.getName();
         if (isStopOwner(o))
         {
            stopTypes.add(o);
         }
         if ("java/lang/Runtime".equals(o) && ("halt".equals(n) || "exit".equals(n)))
         {
            found.add("Runtime." + n);
         }
         else if ("java/lang/System".equals(o) && "exit".equals(n))
         {
            found.add("System.exit");
         }
         else if (isStopOwner(o) && ("load".equals(n) || "loadLibrary".equals(n)))
         {
            found.add("native-load");
         }
         else if (isUnsafe(o))
         {
            found.add("Unsafe");
         }
      }

      /**
       * One constant-pool value, whichever route it arrived by: an {@code ldc},
       * an {@code invokedynamic} bootstrap argument, or the bootstrap arguments
       * of a dynamic constant. The routes differ; what the value means does
       * not, so they share this.
       */
      private void noteConstant(Object value)
      {
         if (value instanceof String)
         {
            strings.add((String) value);
         }
         else if (value instanceof Type)
         {
            noteType((Type) value);
         }
         else if (value instanceof Handle)
         {
            noteHandle((Handle) value);
         }
         else if (value instanceof ConstantDynamic)
         {
            ConstantDynamic cd = (ConstantDynamic) value;
            noteHandle(cd.getBootstrapMethod());
            for (int i = 0; i < cd.getBootstrapMethodArgumentCount(); i++)
            {
               noteConstant(cd.getBootstrapMethodArgument(i));
            }
         }
      }

      @Override
      public void visit(int version, int access, String name, String signature,
                        String superName, String[] interfaces)
      {
         owner = name;
         if (isUnsafe(superName))
         {
            found.add("Unsafe");
         }
      }

      @Override
      public FieldVisitor visitField(int access, String name, String desc,
                                     String signature, Object value)
      {
         if (isUnsafeDesc(desc))
         {
            found.add("Unsafe");
         }
         if (value instanceof String)
         {
            strings.add((String) value);
         }
         return null;
      }

      @Override
      public MethodVisitor visitMethod(int access, String name, String desc,
                                       String signature, String[] exceptions)
      {
         return new MethodVisitor(Opcodes.ASM9)
         {
            @Override
            public void visitMethodInsn(int opcode, String o, String n, String d, boolean itf)
            {
               // Not part of the chain below: an invoke of Runtime.halt is both
               // a hit in its own right and a naming of the owner, and the
               // owner half has to be recorded either way.
               if ("java/lang/Runtime".equals(o))
               {
                  callOwners.add(o);
               }
               if ("java/lang/Runtime".equals(o) && ("halt".equals(n) || "exit".equals(n)))
               {
                  found.add("Runtime." + n);
               }
               else if ("java/lang/System".equals(o) && "exit".equals(n))
               {
                  found.add("System.exit");
               }
               else if (("java/lang/System".equals(o) || "java/lang/Runtime".equals(o))
                        && ("load".equals(n) || "loadLibrary".equals(n)))
               {
                  found.add("native-load");
               }
               else if (isUnsafe(o))
               {
                  found.add("Unsafe." + n);
               }
               else if (isReflection(o, n))
               {
                  reflective = true;
               }
            }

            @Override
            public void visitFieldInsn(int opcode, String o, String n, String d)
            {
               if (isUnsafe(o) || isUnsafeDesc(d))
               {
                  found.add("Unsafe");
               }
            }

            @Override
            public void visitTypeInsn(int opcode, String t)
            {
               // ANEWARRAY of an array type hands back a descriptor rather than
               // an internal name; everything else hands back an internal name.
               if (t != null && t.length() > 0)
               {
                  noteType(t.charAt(0) == '[' ? Type.getType(t) : Type.getObjectType(t));
               }
            }

            @Override
            public void visitLdcInsn(Object value)
            {
               noteConstant(value);
            }

            @Override
            public void visitInvokeDynamicInsn(String n, String d, Handle bsm,
                                               Object... bsmArgs)
            {
               noteHandle(bsm);
               if (bsmArgs != null)
               {
                  for (Object arg : bsmArgs)
                  {
                     noteConstant(arg);
                  }
               }
            }
         };
      }

      /**
       * The reflective route, which needs both halves of the construction: the
       * names, as exact string constants, and a call into the machinery that
       * turns names into an invocation.
       *
       * <p>Strings alone were the first version of this rule and it was wrong.
       * A class that logs the text {@code "java.lang.Runtime"} and concatenates
       * the word {@code "exit"} holds both constants and calls neither method;
       * the image build rehearses exactly that class and requires 0. Conversely
       * {@code Class.forName} alone is ordinary Java. It is the pair that is
       * the signature.
       *
       * <p>The owner half is either a dotted string constant -- the argument
       * {@code Class.forName} takes -- or a reference to the type itself, which
       * is what a {@code Runtime.class} literal leaves in the pool and what the
       * Utf8-only version of this rule could not see. Both are only ever half;
       * the name half and the machinery are still required.
       */
      void finish(TreeSet<String> out)
      {
         if (reflective)
         {
            boolean runtime = strings.contains("java.lang.Runtime")
               || stopTypes.contains("java/lang/Runtime")
               || callOwners.contains("java/lang/Runtime");
            boolean system = strings.contains("java.lang.System")
               || stopTypes.contains("java/lang/System");
            if (runtime && (strings.contains("halt") || strings.contains("exit")))
            {
               found.add("reflect:Runtime");
            }
            if (system && strings.contains("exit"))
            {
               found.add("reflect:System.exit");
            }
            if ((runtime || system)
                && (strings.contains("load") || strings.contains("loadLibrary")))
            {
               found.add("reflect:native-load");
            }
            if (strings.contains("sun.misc.Unsafe")
                || strings.contains("jdk.internal.misc.Unsafe")
                || strings.contains("theUnsafe"))
            {
               found.add("reflect:Unsafe");
            }
         }
         for (String f : found)
         {
            out.add(where + "\t" + owner + "\t" + f);
         }
      }
   }
}
