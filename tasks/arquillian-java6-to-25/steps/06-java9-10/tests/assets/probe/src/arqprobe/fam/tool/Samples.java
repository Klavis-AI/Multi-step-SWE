package arqprobe.fam.tool;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;

import org.jboss.arquillian.impl.client.deployment.tool.ArchiveDeploymentToolingExporter;

/**
 * The inputs the tooling fixtures hand to the formatter and the exporter.
 *
 * Two kinds of thing live here. The sample classes are what a
 * {@code ToolingDeploymentFormatter} is constructed with and what a
 * {@code ClassAsset} is built from — their only interesting property is their
 * name, and the awkward shapes (an inner class, an anonymous class, an array
 * type, a primitive) are here because {@code Class.getName} spells each of them
 * differently and the formatter interpolates that spelling into its XML without
 * looking at it.
 *
 * The other is {@link Exporter}, which exists because the method worth probing
 * on {@code ArchiveDeploymentToolingExporter} is {@code protected}. A subclass
 * is the interface the codebase offers for reaching it; reflection would reach
 * it too, and would also keep working if the method were made private, which is
 * precisely the change a caller should not be insulated from.
 */
public final class Samples
{
   private Samples()
   {
   }

   /** An ordinary top-level class. */
   public static class Alpha
   {
   }

   /** A nested class, whose binary name carries a {@code $}. */
   public static class Inner
   {
      /** One level further down, so the name carries two. */
      public static class Deeper
      {
      }
   }

   /** An anonymous class, whose binary name is a {@code $} and a digit. */
   public static final Object ANONYMOUS = new Object()
   {
   };

   /**
    * Reaches {@code writeOutToFile}, which is protected.
    *
    * Nothing is overridden: the subclass exists to be inside the access
    * boundary, not to change behaviour.
    */
   public static final class Exporter extends ArchiveDeploymentToolingExporter
   {
      public void write(File target, String content)
      {
         writeOutToFile(target, content);
      }
   }

   /**
    * A scratch directory for this fixture, under {@code /tmp} so that
    * {@code Rec.norm} collapses it.
    *
    * One directory per call and never reused, because a fixture that found the
    * previous fixture's file would record a difference that came from the
    * running order rather than from the tree.
    */
   public static File scratch() throws Exception
   {
      File dir = File.createTempFile("arqprobe-tool", "");
      if (!dir.delete() || !dir.mkdirs())
      {
         throw new IllegalStateException("could not create a scratch directory");
      }
      return dir;
   }

   /** A file with known contents, for the {@code FileAsset} and {@code UrlAsset} cases. */
   public static File file(String name, String contents) throws Exception
   {
      File f = new File(scratch(), name);
      OutputStream out = new FileOutputStream(f);
      try
      {
         out.write(contents.getBytes("UTF-8"));
      }
      finally
      {
         out.close();
      }
      return f;
   }
}
