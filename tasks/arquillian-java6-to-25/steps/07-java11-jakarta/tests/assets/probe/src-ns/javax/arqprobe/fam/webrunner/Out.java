package arqprobe.fam.webrunner;

import java.io.ByteArrayOutputStream;
import java.io.IOException;

import javax.servlet.ServletOutputStream;

/**
 * The javax half of the {@code ServletOutputStream} the response proxy hands
 * out. See {@code src-ns/jakarta/.../Out.java} for the other half, and
 * {@code probe/run.sh} for which one is compiled.
 *
 * Servlet 2.5 leaves {@code OutputStream.write(int)} as the only abstract
 * method to answer. Everything above that -- {@code write(byte[], int, int)},
 * {@code flush}, {@code close} -- is inherited, so every byte the servlet
 * writes arrives here one at a time and {@link #failAfter} can cut the stream
 * at an exact offset.
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
}
