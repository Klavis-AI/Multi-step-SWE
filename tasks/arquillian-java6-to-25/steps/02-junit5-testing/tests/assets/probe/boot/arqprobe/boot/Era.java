package arqprobe.boot;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;

/**
 * Which enterprise-API namespace the tree under test was compiled against.
 *
 * Printed on stdout as one word, {@code javax} or {@code jakarta}, and used by
 * {@code run.sh} to bind the probe harness's own sources to the same namespace
 * before compiling them. Ten of the packages the harness has to name are
 * renamed partway up the ladder ({@code javax.ejb} becomes {@code jakarta.ejb},
 * and so on); a harness that hard-named either one would stop compiling on one
 * side of that step and take the whole behaviour suite with it.
 *
 * The answer is read out of the tree rather than guessed, and it is read
 * without linking anything: the bytes of a class that is compiled against the
 * namespace are scanned for the package name, which a class file stores
 * verbatim in its constant pool. Loading the class instead would need the API
 * jar on the classpath before we know which API jar to put there.
 *
 * Runs on the JDK alone. It is compiled and executed before the harness exists.
 */
public final class Era
{
   /**
    * A class in the tree that cannot stop referring to the namespace: its whole
    * job is to implement a CDI SPI interface.
    */
   private static final String WITNESS =
         "org/jboss/arquillian/testenricher/cdi/MethodParameterInjectionPoint.class";

   private Era()
   {
   }

   public static void main(String[] args) throws Exception
   {
      System.out.println(namespace());
   }

   static String namespace() throws Exception
   {
      byte[] bytes = witness();
      boolean jakarta = contains(bytes, "jakarta/enterprise/inject/spi/InjectionPoint");
      boolean javax = contains(bytes, "javax/enterprise/inject/spi/InjectionPoint");
      if (jakarta == javax)
      {
         // Both or neither: the witness is not the class we think it is, and a
         // default here would be a guess that compiles and then records the
         // wrong thing. Refuse instead.
         throw new IllegalStateException("ERA-UNDECIDABLE: " + WITNESS
               + " names jakarta=" + jakarta + " javax=" + javax);
      }
      return jakarta ? "jakarta" : "javax";
   }

   private static byte[] witness() throws Exception
   {
      InputStream in = Era.class.getClassLoader().getResourceAsStream(WITNESS);
      if (in == null)
      {
         throw new IllegalStateException("ERA-NO-WITNESS: " + WITNESS + " is not on the classpath");
      }
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

   private static boolean contains(byte[] haystack, String needle)
   {
      byte[] n = new byte[needle.length()];
      for (int i = 0; i < needle.length(); i++)
      {
         n[i] = (byte) needle.charAt(i);
      }
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
