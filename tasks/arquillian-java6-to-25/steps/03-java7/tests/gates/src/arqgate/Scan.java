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
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.ModuleVisitor;
import org.objectweb.asm.Opcodes;

/**
 * The bytecode half of the migrated-ness gates: one pass over the built
 * artifacts producing a flat, sorted, machine-readable report.
 *
 * <pre>
 *   java -cp arqgate.jar:asm.jar arqgate.Scan &lt;out.txt&gt; &lt;jar-or-dir&gt;...
 * </pre>
 *
 * <p>Writes {@code out.txt} (one line per aggregate fact) and
 * {@code out.txt.pairs} (one line per class-to-type reference, so a gate can
 * name the classes that still carry a finding instead of only counting them).
 *
 * <p><b>It measures, and never decides.</b> There is no watch list here and no
 * threshold. Every judgement -- which class-file major version this step
 * requires, which platform types must be gone by now, which packages are
 * boundary packages -- lives in the step's own verifier, so one tool serves all
 * thirteen steps and a step's expectations stay readable in one place.
 *
 * <p><b>Why the constant pool and not the instruction stream.</b> The task line
 * asks for a constant-pool scan in as many words, and the reason is in the same
 * sentence: <em>reflection and string tricks still leave type refs in the
 * constant pool</em>. A tree that replaced {@code new Vector()} with
 * {@code Class.forName("java.util.Vector").newInstance()} has no {@code NEW}
 * instruction naming the type and no {@code CONSTANT_Class} entry either -- but
 * it does have the {@code CONSTANT_Utf8} {@code "java.util.Vector"}, because the
 * string literal has to live somewhere. So the pool is read two ways:
 *
 * <ul>
 *   <li>{@code typeref} -- the structural view. Every {@code CONSTANT_Class}
 *       (tag 7), plus every type named inside a descriptor or generic signature,
 *       which is where a field type, a parameter type, a return type and a type
 *       argument actually live. Internal form, {@code java/util/Vector}.
 *   <li>{@code strref} -- the reflective view. Every {@code CONSTANT_Utf8} whose
 *       whole text reads as a dotted platform class name. Source form,
 *       {@code java.util.Vector}.
 * </ul>
 *
 * <p>Both are limited to the platform namespaces ({@code java}, {@code javax},
 * {@code jakarta}, {@code sun}, {@code com.sun}, {@code jdk}); the tree's own
 * classes are counted but not enumerated, because no gate here is about them.
 *
 * <p><b>Known limit, stated rather than papered over.</b> A name assembled at
 * run time -- {@code "java.util." + "Vector"} -- is in no pool entry, and no
 * static scan can see it. What stops that route being useful is the behaviour
 * suite: a tree hiding a type this way still has to reproduce the recorded
 * transcripts, so the type is still doing its old job and is still there to be
 * found by whoever reads the diff.
 *
 * <p>Output is line-oriented {@code key=value} / {@code key[i]=value}, the same
 * shape the behaviour transcripts use, so a diff against a recorded expectation
 * reads the same way whichever family produced it.
 */
public final class Scan
{
   /** Namespaces a migrated-ness gate can be about. */
   private static boolean platform(String internalName)
   {
      return internalName.startsWith("java/")
         || internalName.startsWith("javax/")
         || internalName.startsWith("jakarta/")
         || internalName.startsWith("sun/")
         || internalName.startsWith("com/sun/")
         || internalName.startsWith("jdk/");
   }

   public static void main(String[] args) throws Exception
   {
      if (args.length < 2)
      {
         System.err.println("usage: Scan <out.txt> <jar-or-dir>...");
         System.exit(2);
      }
      List<File> roots = new ArrayList<File>();
      for (int i = 1; i < args.length; i++)
      {
         roots.add(new File(args[i]));
      }
      Collections.sort(roots);

      Scan scan = new Scan();
      for (File root : roots)
      {
         scan.walk(root);
      }

      PrintStream out = new PrintStream(new File(args[0]), "UTF-8");
      try
      {
         scan.report(out);
      }
      finally
      {
         out.close();
      }
      PrintStream pairs = new PrintStream(new File(args[0] + ".pairs"), "UTF-8");
      try
      {
         for (String pair : scan.pairs)
         {
            pairs.println(pair);
         }
         pairs.println("pairs.size=" + scan.pairs.size());
      }
      finally
      {
         pairs.close();
      }
      System.out.println("arqgate: classes=" + scan.classes + " roots=" + roots.size()
         + " typerefs=" + scan.typerefs.size() + " strrefs=" + scan.strrefs.size());
   }

   // ---------------------------------------------------------------- state

   private int classes;
   /** major version -> how many classes carry it. */
   private final TreeMap<Integer, Integer> majors = new TreeMap<Integer, Integer>();
   /** platform type, internal form -> how many classes reference it structurally. */
   private final TreeMap<String, Integer> typerefs = new TreeMap<String, Integer>();
   /** platform type, dotted form -> how many classes carry it as a string constant. */
   private final TreeMap<String, Integer> strrefs = new TreeMap<String, Integer>();
   /** "&lt;class&gt; &lt;method&gt;&lt;desc&gt;" for every method with ACC_NATIVE. */
   private final TreeSet<String> nativeMethods = new TreeSet<String>();
   /** Every distinct package that owns at least one class. */
   private final TreeSet<String> packages = new TreeSet<String>();
   /**
    * "&lt;class&gt; extends &lt;super&gt; implements &lt;a,b&gt;" for every
    * {@code Outer$1} the source actually wrote. The supertype is on the line
    * because "anonymous functional-interface class" is the finding, and an
    * anonymous subclass of an abstract class -- {@code new
    * ThreadLocal&lt;...&gt;(){...}}, which this tree has -- is not one.
    */
   private final TreeSet<String> anonymousClasses = new TreeSet<String>();
   /**
    * The {@code Outer$1} classes javac invented, kept apart from the ones above.
    *
    * <p>A switch over an enum compiles to a synthetic {@code Outer$1} holding a
    * {@code $SwitchMap$...} array. It is named like an anonymous class and is
    * not one: no source statement created it and none can delete it short of
    * rewriting the switch. Counting it as a finding would put a requirement in a
    * late step that an earlier step's own code makes unsatisfiable, so the two
    * are separate lists and a gate says which one it means.
    */
   private final TreeSet<String> syntheticInner = new TreeSet<String>();
   /** One line per module-info.class, naming the module. */
   private final TreeSet<String> modules = new TreeSet<String>();
   /** "&lt;class&gt; &lt;kind&gt; &lt;type&gt;", the per-class detail behind the counts. */
   private final TreeSet<String> pairs = new TreeSet<String>();

   // ---------------------------------------------------------------- walk

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
            // Zip order is whatever the packager felt like; sort so two runs of
            // the same tree produce the same report byte for byte.
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

   // ---------------------------------------------------------------- visit

   private void visit(byte[] bytes)
   {
      ClassReader cr = new ClassReader(bytes);
      final String owner = cr.getClassName();
      classes++;

      // Bytes 6-7 of every class file are the major version. Read from the
      // artifact, not inferred from the toolchain that is supposed to have
      // produced it.
      int major = ((bytes[6] & 0xff) << 8) | (bytes[7] & 0xff);
      bump(majors, Integer.valueOf(major));

      int slash = owner.lastIndexOf('/');
      packages.add(slash < 0 ? "<default>" : owner.substring(0, slash).replace('/', '.'));

      cr.accept(new ClassVisitor(Opcodes.ASM9)
      {
         @Override
         public void visit(int version, int access, String name, String signature,
                           String superName, String[] interfaces)
         {
            if (isAnonymous(name))
            {
               StringBuilder sb = new StringBuilder(name);
               sb.append(" extends ").append(superName == null ? "-" : superName);
               sb.append(" implements ");
               if (interfaces == null || interfaces.length == 0)
               {
                  sb.append('-');
               }
               else
               {
                  String[] sorted = interfaces.clone();
                  Arrays.sort(sorted);
                  for (int i = 0; i < sorted.length; i++)
                  {
                     if (i > 0)
                     {
                        sb.append(',');
                     }
                     sb.append(sorted[i]);
                  }
               }
               if ((access & Opcodes.ACC_SYNTHETIC) != 0)
               {
                  syntheticInner.add(sb.toString());
               }
               else
               {
                  anonymousClasses.add(sb.toString());
               }
            }
         }

         @Override
         public ModuleVisitor visitModule(String name, int access, String version)
         {
            modules.add(name);
            return null;
         }

         @Override
         public MethodVisitor visitMethod(int access, String name, String desc,
                                          String sig, String[] ex)
         {
            if ((access & Opcodes.ACC_NATIVE) != 0)
            {
               nativeMethods.add(owner + " " + name + desc);
            }
            return null;
         }
      }, ClassReader.SKIP_CODE | ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);

      scanConstantPool(cr, bytes, owner);
   }

   /** {@code Outer$12} -- digits only after the last {@code $}. */
   private static boolean isAnonymous(String internalName)
   {
      int dollar = internalName.lastIndexOf('$');
      if (dollar < 0 || dollar == internalName.length() - 1)
      {
         return false;
      }
      for (int i = dollar + 1; i < internalName.length(); i++)
      {
         if (!Character.isDigit(internalName.charAt(i)))
         {
            return false;
         }
      }
      return true;
   }

   /**
    * Walk the raw constant pool once per class.
    *
    * <p>ASM indexes the pool for us -- {@code getItem(i)} is the offset of entry
    * {@code i}'s content, so the byte before it is the tag -- but it will not
    * hand back a {@code CONSTANT_Utf8} by index, and the Utf8 entries are the
    * whole point (see the class comment). So tag 7 goes through
    * {@code readUTF8} and tag 1 is decoded from the bytes directly.
    *
    * <p>Counting is per class, not per pool entry: a class naming
    * {@code java/util/Vector} forty times counts once, because the question a
    * gate asks is how many classes still carry the type. The site-level detail
    * rule 11 wants is in the {@code .pairs} file, and the one place a count of
    * expressions really matters -- Error Prone findings -- is Error Prone's job,
    * not this scanner's.
    */
   private void scanConstantPool(ClassReader cr, byte[] bytes, String owner)
   {
      char[] buf = new char[cr.getMaxStringLength()];
      TreeSet<String> hereTypes = new TreeSet<String>();
      TreeSet<String> hereStrings = new TreeSet<String>();
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
            if (text != null)
            {
               // An array class appears as "[Ljava/util/Vector;"; the element
               // type is the reference that matters.
               addType(hereTypes, element(text));
            }
         }
         else if (tag == 1) // CONSTANT_Utf8
         {
            String text = utf8(bytes, off);
            if (text == null)
            {
               continue;
            }
            harvestDescriptor(hereTypes, text);
            if (looksLikeDottedClassName(text) && platform(text.replace('.', '/')))
            {
               hereStrings.add(text);
            }
         }
      }
      for (String t : hereTypes)
      {
         bump(typerefs, t);
         pairs.add(owner + " typeref " + t);
      }
      for (String s : hereStrings)
      {
         bump(strrefs, s);
         pairs.add(owner + " strref " + s);
      }
   }

   /** {@code [[Ljava/util/Vector;} -> the element type; {@code [I} -> null. */
   private static String element(String internalOrDescriptor)
   {
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

   private static void addType(TreeSet<String> into, String internalName)
   {
      if (internalName != null && platform(internalName))
      {
         into.add(internalName);
      }
   }

   /**
    * Pull every {@code Lsome/Type;} out of a descriptor or generic signature.
    *
    * <p>Applied to every Utf8 entry rather than only to the ones a descriptor
    * slot points at: that costs a scan of text that is usually not a descriptor,
    * and buys immunity to the shapes -- {@code Signature} attributes,
    * {@code MethodType} constants, annotation element values -- where a type
    * name sits in a Utf8 that no field or method descriptor references.
    */
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
            addType(into, text.substring(l + 1, end));
         }
         from = l + 1;
      }
   }

   /** {@code java.util.Vector} yes; {@code java.util.} no; {@code hello} no. */
   private static boolean looksLikeDottedClassName(String text)
   {
      if (text.length() < 3 || text.indexOf('.') < 0 || text.indexOf('/') >= 0
         || text.indexOf(' ') >= 0 || text.indexOf(';') >= 0)
      {
         return false;
      }
      if (text.charAt(0) == '.' || text.charAt(text.length() - 1) == '.')
      {
         return false;
      }
      for (int i = 0; i < text.length(); i++)
      {
         char c = text.charAt(i);
         if (c != '.' && c != '$' && !Character.isJavaIdentifierPart(c))
         {
            return false;
         }
      }
      return true;
   }

   /** Modified UTF-8, as the class-file format defines it. */
   private static String utf8(byte[] b, int off)
   {
      int len = ((b[off] & 0xff) << 8) | (b[off + 1] & 0xff);
      StringBuilder sb = new StringBuilder(len);
      int i = off + 2;
      int end = i + len;
      while (i < end)
      {
         int c = b[i++] & 0xff;
         if (c < 0x80)
         {
            sb.append((char) c);
         }
         else if ((c & 0xe0) == 0xc0)
         {
            sb.append((char) (((c & 0x1f) << 6) | (b[i++] & 0x3f)));
         }
         else
         {
            sb.append((char) (((c & 0xf) << 12) | ((b[i++] & 0x3f) << 6) | (b[i++] & 0x3f)));
         }
      }
      return sb.toString();
   }

   private static <K> void bump(Map<K, Integer> counts, K key)
   {
      Integer n = counts.get(key);
      counts.put(key, Integer.valueOf(n == null ? 1 : n.intValue() + 1));
   }

   // ---------------------------------------------------------------- report

   private void report(PrintStream out)
   {
      out.println("classes=" + classes);
      for (Map.Entry<Integer, Integer> e : majors.entrySet())
      {
         out.println("major." + e.getKey() + "=" + e.getValue());
      }
      out.println("majors.distinct=" + majors.size());
      emit(out, "package", packages);
      emit(out, "native", nativeMethods);
      emit(out, "anonymous", anonymousClasses);
      emit(out, "syntheticinner", syntheticInner);
      emit(out, "module", modules);
      emitCounts(out, "typeref", typerefs);
      emitCounts(out, "strref", strrefs);
   }

   private static void emit(PrintStream out, String key, TreeSet<String> values)
   {
      int i = 0;
      for (String v : values)
      {
         out.println(key + "[" + (i++) + "]=" + v);
      }
      out.println(key + ".size=" + values.size());
   }

   private static <K> void emitCounts(PrintStream out, String key, TreeMap<K, Integer> counts)
   {
      int i = 0;
      for (Map.Entry<K, Integer> e : counts.entrySet())
      {
         out.println(key + "[" + (i++) + "]=" + e.getValue() + " " + e.getKey());
      }
      out.println(key + ".size=" + counts.size());
   }
}
