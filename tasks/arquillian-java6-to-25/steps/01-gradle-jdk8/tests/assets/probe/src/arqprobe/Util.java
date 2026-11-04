package arqprobe;

import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.util.ArrayList;
import java.util.List;

public final class Util
{
   private Util()
   {
   }

   public static List<String> readLines(File f) throws Exception
   {
      List<String> out = new ArrayList<String>();
      BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
      try
      {
         String l;
         while ((l = r.readLine()) != null)
         {
            out.add(l.trim());
         }
      }
      finally
      {
         r.close();
      }
      return out;
   }

   public static byte[] readAll(InputStream in) throws Exception
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

   public static byte[] serialize(Object o) throws Exception
   {
      ByteArrayOutputStream bos = new ByteArrayOutputStream();
      ObjectOutputStream oos = new ObjectOutputStream(bos);
      oos.writeObject(o);
      oos.close();
      return bos.toByteArray();
   }

   public static Object deserialize(byte[] b) throws Exception
   {
      ObjectInputStream ois = new ObjectInputStream(new ByteArrayInputStream(b));
      try
      {
         return ois.readObject();
      }
      finally
      {
         ois.close();
      }
   }

   /**
    * Whether a class-file path names a class javac invented rather than one the
    * author declared: {@code Foo$1.class}, {@code Foo$2Bar.class}.
    *
    * <p>The probe records behaviour, and this is the line between behaviour and
    * the compiler's strategy for producing it. An anonymous class is the latter.
    * {@code new PrivilegedAction() { ... }} and {@code () -> ...} do the same
    * thing at the same moment with the same result; only one of them leaves a
    * {@code $1.class} behind. Once a step asks for lambdas — and this ladder
    * asks for 36 of them at Java 8, more later — a listing that names
    * {@code $1.class} fails a correct migration and passes a refusal to migrate.
    * That is a fixture measuring the compiler, so the fixture stops looking.
    *
    * <p>Declared nested classes ({@code Foo$Bar.class}) are kept: those are
    * names the author chose, and a packager dropping one is a real fault.
    *
    * <p>The same line is drawn on the scoring side by {@code gates/apisig.sh},
    * which has always dropped {@code $[0-9]} from the API surface.
    */
   public static boolean compilerMade(String path)
   {
      if (!path.endsWith(".class"))
      {
         return false;
      }
      for (int i = path.lastIndexOf('/') + 1; i + 1 < path.length(); i++)
      {
         if (path.charAt(i) == '$' && Character.isDigit(path.charAt(i + 1)))
         {
            return true;
         }
      }
      return false;
   }
}
