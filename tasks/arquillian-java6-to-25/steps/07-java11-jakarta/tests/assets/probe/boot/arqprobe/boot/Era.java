package arqprobe.boot;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Which enterprise-API namespace the tree under test was compiled against,
 * ONE PACKAGE AT A TIME.
 *
 * Printed on stdout as one {@code <package>=<verdict>} line per package named
 * on the command line, where the verdict is {@code javax}, {@code jakarta} or
 * {@code absent}, and used by {@code run.sh} to bind the probe harness's own
 * sources to the same namespace before compiling them.
 *
 * PER PACKAGE, AND NOT ONE WORD FOR THE WHOLE TREE, because the tree is not one
 * word. The Jakarta rename did not arrive here all at once and could not: this
 * tree's CDI enricher is pinned to cdi-api 1.0 and its servlet protocol to
 * Jetty 6.1.9, so {@code javax.enterprise} and {@code javax.servlet} stay on
 * javax for several rungs after {@code javax.ejb} and
 * {@code javax.annotation.Resource} have left. An earlier version of this class
 * answered with a single word read off a single witness class -- and the
 * witness it named, {@code MethodParameterInjectionPoint}, lives in the one
 * module that never moves. It answered "javax" for a tree whose enrichers were
 * already on jakarta, the harness kept its javax annotations, and 36 enricher
 * fixtures recorded {@code instance=null}: a broken suite that would have been
 * paid for as a migration. The instrument was the finding. See NOTES 12.10.
 *
 * The answer is read out of the tree rather than guessed, and it is read
 * without linking anything: the bytes of a class that is compiled against the
 * namespace name it verbatim in the constant pool. Loading the class instead
 * would need the API jar on the classpath before we know which API jar to put
 * there.
 *
 * A package that appears in BOTH forms on one tree is refused rather than
 * resolved. The harness binds a package name, not a file, so it has no way to
 * name both; a default here would compile and then record against a coin flip.
 *
 * Runs on the JDK alone. It is compiled and executed before the harness exists.
 */
public final class Era
{
   private Era()
   {
   }

   public static void main(String[] args) throws Exception
   {
      if (args.length == 0)
      {
         throw new IllegalStateException("ERA-NO-PACKAGES: name the renamed packages as arguments");
      }
      byte[][] jakPat = new byte[args.length][];
      byte[][] javPat = new byte[args.length][];
      boolean[] jak = new boolean[args.length];
      boolean[] jav = new boolean[args.length];
      for (int i = 0; i < args.length; i++)
      {
         String slash = args[i].replace('.', '/');
         if (!slash.startsWith("javax/"))
         {
            throw new IllegalStateException("ERA-BAD-PACKAGE: " + args[i] + " is not a javax name");
         }
         javPat[i] = ascii(slash);
         jakPat[i] = ascii("jakarta/" + slash.substring("javax/".length()));
      }

      int scanned = 0;
      for (String entry : System.getProperty("java.class.path").split(File.pathSeparator))
      {
         File f = new File(entry);
         if (f.isFile() && entry.endsWith(".jar"))
         {
            scanned += scanJar(f, jakPat, javPat, jak, jav);
         }
         else if (f.isDirectory())
         {
            scanned += scanDir(f, jakPat, javPat, jak, jav);
         }
      }
      if (scanned == 0)
      {
         throw new IllegalStateException("ERA-NO-CLASSES: nothing on the classpath to read");
      }

      StringBuilder out = new StringBuilder();
      for (int i = 0; i < args.length; i++)
      {
         if (jak[i] && jav[i])
         {
            throw new IllegalStateException("ERA-MIXED: " + args[i]
                  + " appears on this tree in BOTH namespaces; the harness cannot bind to either");
         }
         out.append(args[i]).append('=')
            .append(jak[i] ? "jakarta" : jav[i] ? "javax" : "absent").append('\n');
      }
      System.out.print(out);
   }

   private static int scanJar(File jar, byte[][] jakPat, byte[][] javPat, boolean[] jak, boolean[] jav)
         throws Exception
   {
      int n = 0;
      ZipFile zf = new ZipFile(jar);
      try
      {
         Enumeration<? extends ZipEntry> en = zf.entries();
         while (en.hasMoreElements())
         {
            ZipEntry ze = en.nextElement();
            if (ze.isDirectory() || !ze.getName().endsWith(".class"))
            {
               continue;
            }
            InputStream in = zf.getInputStream(ze);
            try
            {
               mark(drain(in), jakPat, javPat, jak, jav);
            }
            finally
            {
               in.close();
            }
            n++;
         }
      }
      finally
      {
         zf.close();
      }
      return n;
   }

   private static int scanDir(File dir, byte[][] jakPat, byte[][] javPat, boolean[] jak, boolean[] jav)
         throws Exception
   {
      int n = 0;
      List<File> stack = new ArrayList<File>();
      stack.add(dir);
      while (!stack.isEmpty())
      {
         File f = stack.remove(stack.size() - 1);
         File[] kids = f.listFiles();
         if (kids == null)
         {
            continue;
         }
         for (File k : kids)
         {
            if (k.isDirectory())
            {
               stack.add(k);
            }
            else if (k.getName().endsWith(".class"))
            {
               InputStream in = new FileInputStream(k);
               try
               {
                  mark(drain(in), jakPat, javPat, jak, jav);
               }
               finally
               {
                  in.close();
               }
               n++;
            }
         }
      }
      return n;
   }

   private static void mark(byte[] bytes, byte[][] jakPat, byte[][] javPat, boolean[] jak, boolean[] jav)
   {
      for (int i = 0; i < jakPat.length; i++)
      {
         if (!jak[i] && contains(bytes, jakPat[i]))
         {
            jak[i] = true;
         }
         if (!jav[i] && contains(bytes, javPat[i]))
         {
            jav[i] = true;
         }
      }
   }

   private static byte[] drain(InputStream in) throws Exception
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

   private static byte[] ascii(String s)
   {
      byte[] b = new byte[s.length()];
      for (int i = 0; i < s.length(); i++)
      {
         b[i] = (byte) s.charAt(i);
      }
      return b;
   }

   private static boolean contains(byte[] haystack, byte[] n)
   {
      outer: for (int i = 0; i + n.length <= haystack.length; i++)
      {
         for (int j = 0; j < n.length; j++)
         {
            if (haystack[i + j] != n[j])
            {
               continue outer;
            }
         }
         return true;
      }
      return false;
   }
}
