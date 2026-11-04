package arqprobe.fam.webrunner;

import java.io.ByteArrayOutputStream;
import java.io.IOException;

import jakarta.servlet.ServletOutputStream;
import jakarta.servlet.WriteListener;

/**
 * The jakarta half of the {@code ServletOutputStream} the response proxy hands
 * out. See {@code src-ns/javax/.../Out.java} for the other half, and
 * {@code probe/run.sh} for which one is compiled.
 *
 * Servlet 5.0 adds two abstract methods that Servlet 2.5 does not have, which
 * is the whole reason this file exists rather than a sed rule: the non-blocking
 * API arrived in 3.1 and {@code WriteListener} is not a type the 2.5 jar can
 * name. Neither is ever called here -- the fixtures drive a blocking write --
 * but both have to be answered for the class to be concrete.
 *
 * The two copies must stay behaviourally identical: a fixture records byte
 * counts, and a count that moved because the overlay drifted would read as a
 * change in the tree under test.
 */
public final class Out extends ServletOutputStream
{
   private final ByteArrayOutputStream sink = new ByteArrayOutputStream();

   /** Offset at which writing starts failing, or -1 for a stream that never fails. */
   private int failAfter = -1;

   public void failAfter(int bytes)
   {
      this.failAfter = bytes;
   }

   public int size()
   {
      return sink.size();
   }

   public byte[] bytes()
   {
      return sink.toByteArray();
   }

   @Override
   public void write(int b) throws IOException
   {
      if (failAfter >= 0 && sink.size() >= failAfter)
      {
         throw new IOException("the connection was closed after " + failAfter + " byte(s)");
      }
      sink.write(b);
   }

   @Override
   public boolean isReady()
   {
      return true;
   }

   @Override
   public void setWriteListener(WriteListener listener)
   {
      throw new UnsupportedOperationException("the fixtures write blocking");
   }
}
