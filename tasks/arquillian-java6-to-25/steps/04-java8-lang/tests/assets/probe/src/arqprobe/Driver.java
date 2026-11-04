package arqprobe;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.util.List;

/**
 * Runs a list of fixtures and writes one transcript file per fixture.
 *
 * <pre>
 * java arqprobe.Driver &lt;out-dir&gt; &lt;inputs-dir&gt; &lt;id-list-file&gt;
 * </pre>
 *
 * Each fixture runs on a fresh thread so that thread-local state left behind by
 * one cannot be observed by the next, and a fixture that throws produces a
 * transcript recording the throwable rather than killing the batch.
 */
public class Driver
{
   public static void main(String[] args) throws Exception
   {
      File outDir = new File(args[0]);
      File inputs = new File(args[1]);
      outDir.mkdirs();
      List<String> ids = Util.readLines(new File(args[2]));

      int run = 0;
      int failed = 0;
      for (String id : ids)
      {
         if (id.length() == 0 || id.startsWith("#"))
         {
            continue;
         }
         run++;
         String text = runOne(id, inputs);
         write(new File(outDir, id.replace('/', '.') + ".txt"), text);
         if (text.startsWith("FIXTURE-ERROR"))
         {
            failed++;
            System.out.println("ERROR " + id);
         }
      }
      System.out.println("fixtures=" + run + " errors=" + failed);
   }

   static String runOne(final String id, final File inputs)
   {
      final String[] box = new String[1];
      Runnable body = new Runnable()
      {
         public void run()
         {
            Rec r = new Rec();
            try
            {
               Registry.create(id, inputs).run(r);
               box[0] = r.text();
            }
            catch (Throwable t)
            {
               // A fixture records the errors it provokes; an escape is a
               // harness bug and is loud rather than silently scored.
               StringBuilder sb = new StringBuilder();
               sb.append("FIXTURE-ERROR ").append(id).append('\n');
               sb.append(t.getClass().getName()).append(": ")
                 .append(Rec.norm(String.valueOf(t.getMessage()))).append('\n');
               StackTraceElement[] st = t.getStackTrace();
               for (int i = 0; i < st.length && i < 12; i++)
               {
                  sb.append("  at ").append(st[i]).append('\n');
               }
               sb.append("--- partial transcript ---\n").append(r.text());
               box[0] = sb.toString();
            }
         }
      };
      Thread th = new Thread(body, "fixture-" + id);
      th.setContextClassLoader(Driver.class.getClassLoader());
      th.start();
      try
      {
         th.join(180000);
      }
      catch (InterruptedException e)
      {
         Thread.currentThread().interrupt();
      }
      if (box[0] == null)
      {
         return "FIXTURE-ERROR " + id + "\ntimeout or thread death\n";
      }
      return box[0];
   }

   static void write(File f, String text) throws Exception
   {
      f.getParentFile().mkdirs();
      Writer w = new OutputStreamWriter(new FileOutputStream(f), "UTF-8");
      try
      {
         w.write(text);
      }
      finally
      {
         w.close();
      }
   }
}
