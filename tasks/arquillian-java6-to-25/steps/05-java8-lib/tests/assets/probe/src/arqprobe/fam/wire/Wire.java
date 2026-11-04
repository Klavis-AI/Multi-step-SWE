package arqprobe.fam.wire;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.ObjectStreamClass;
import java.io.ObjectStreamField;
import java.util.ArrayList;
import java.util.List;

import arqprobe.Rec;

/**
 * Serialization helpers for the {@code wire} family.
 *
 * Nothing here records raw stream bytes: a payload that carries a stack trace
 * embeds JDK-owned class descriptors, and those are not the codebase's contract.
 * What is recorded is the codebase's own serialized <em>shape</em> and what
 * survives a round trip.
 */
public final class Wire
{
   private Wire()
   {
   }

   public static byte[] write(Object o) throws IOException
   {
      ByteArrayOutputStream bytes = new ByteArrayOutputStream();
      ObjectOutputStream out = new ObjectOutputStream(bytes);
      try
      {
         out.writeObject(o);
      }
      finally
      {
         out.close();
      }
      return bytes.toByteArray();
   }

   public static Object read(byte[] b) throws IOException, ClassNotFoundException
   {
      ObjectInputStream in = new ObjectInputStream(new ByteArrayInputStream(b));
      try
      {
         return in.readObject();
      }
      finally
      {
         in.close();
      }
   }

   public static Object roundTrip(Object o) throws IOException, ClassNotFoundException
   {
      return read(write(o));
   }

   /**
    * Rewrite one {@code TC_STRING} in a serialized stream.
    *
    * The class name a proxy carries is just a string on the wire, so this is how
    * a payload that names a type the client does not have is produced — which is
    * the case {@code ExceptionProxy} exists to handle.
    */
   public static byte[] replaceString(byte[] in, String from, String to)
   {
      byte[] f = utf(from);
      byte[] t = utf(to);
      byte[] needle = new byte[3 + f.length];
      needle[0] = (byte) 0x74; // TC_STRING
      needle[1] = (byte) ((f.length >> 8) & 0xff);
      needle[2] = (byte) (f.length & 0xff);
      System.arraycopy(f, 0, needle, 3, f.length);

      int at = indexOf(in, needle);
      if (at < 0)
      {
         throw new IllegalStateException("no TC_STRING \"" + from + "\" in stream");
      }

      byte[] out = new byte[in.length - needle.length + 3 + t.length];
      System.arraycopy(in, 0, out, 0, at);
      out[at] = (byte) 0x74;
      out[at + 1] = (byte) ((t.length >> 8) & 0xff);
      out[at + 2] = (byte) (t.length & 0xff);
      System.arraycopy(t, 0, out, at + 3, t.length);
      System.arraycopy(in, at + needle.length, out, at + 3 + t.length,
            in.length - at - needle.length);
      return out;
   }

   /**
    * Record a class's serialized shape: the identity a peer built against an
    * older copy of this codebase will check the payload against.
    */
   public static void shape(Rec r, String key, Class<?> c)
   {
      ObjectStreamClass osc = ObjectStreamClass.lookup(c);
      if (osc == null)
      {
         r.kv(key, "<not-serializable>");
         return;
      }
      r.kv(key + ".name", osc.getName());
      r.kv(key + ".serialVersionUID", Long.toString(osc.getSerialVersionUID()));
      ObjectStreamField[] fields = osc.getFields();
      List<String> out = new ArrayList<String>();
      for (int i = 0; i < fields.length; i++)
      {
         out.add(fields[i].getName() + ":"
               + (fields[i].isPrimitive()
                     ? String.valueOf(fields[i].getTypeCode())
                     : fields[i].getTypeString()));
      }
      r.sorted(key + ".fields", out);
   }

   /** Frames as the codebase sees them, without going through a JDK-formatted toString. */
   public static void frames(Rec r, String key, Throwable t)
   {
      if (t == null)
      {
         r.kv(key + ".size", "<no-throwable>");
         return;
      }
      StackTraceElement[] st = t.getStackTrace();
      List<String> out = new ArrayList<String>();
      for (int i = 0; i < st.length; i++)
      {
         out.add(st[i].getClassName() + "." + st[i].getMethodName()
               + "(" + st[i].getFileName() + ":" + st[i].getLineNumber() + ")");
      }
      r.list(key, out);
   }

   /** Type and message of every link in a cause chain, in order. */
   public static void chain(Rec r, String key, Throwable t)
   {
      List<String> out = new ArrayList<String>();
      for (String[] link : Rec.chainOf(t))
      {
         out.add(link[0] + ": " + link[1]);
      }
      r.list(key, out);
   }

   private static byte[] utf(String s)
   {
      try
      {
         return s.getBytes("UTF-8");
      }
      catch (java.io.UnsupportedEncodingException e)
      {
         throw new IllegalStateException(e);
      }
   }

   private static int indexOf(byte[] haystack, byte[] needle)
   {
      outer:
      for (int i = 0; i + needle.length <= haystack.length; i++)
      {
         for (int j = 0; j < needle.length; j++)
         {
            if (haystack[i + j] != needle[j])
            {
               continue outer;
            }
         }
         return i;
      }
      return -1;
   }
}
