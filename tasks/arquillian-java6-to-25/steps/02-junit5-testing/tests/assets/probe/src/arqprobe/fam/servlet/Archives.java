package arqprobe.fam.servlet;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;

import org.jboss.shrinkwrap.api.Archive;
import org.jboss.shrinkwrap.api.ArchivePath;
import org.jboss.shrinkwrap.api.Node;
import org.jboss.shrinkwrap.api.asset.Asset;
import org.jboss.shrinkwrap.impl.base.asset.ArchiveAsset;

import arqprobe.Util;

/**
 * Building the deployments the packagers are asked to package, and reading back
 * what they made of them.
 *
 * A packager's whole output is an archive, so the projection of an archive is
 * this family's most important recorder. Two decisions in it are worth stating.
 *
 * <p><b>It recurses.</b> Both Servlet packagers do their real work one level
 * down — the Servlet 3.0 one adds its protocol archive as a library inside the
 * web archive, and both of them reach into the single {@code .war} nested in an
 * enterprise archive and modify <em>that</em>. A flat listing of the returned
 * archive's own paths would show `/WEB-INF/lib/arquillian-protocol.jar` either
 * way and could not tell an empty jar from the real one.
 *
 * <p><b>It names a ShrinkWrap implementation class.</b> Detecting a nested
 * archive means asking whether a {@code Node}'s asset is an
 * {@code org.jboss.shrinkwrap.impl.base.asset.ArchiveAsset}, which is not part
 * of ShrinkWrap's API package. That is deliberate and it is not reaching past
 * the codebase's surface: <em>both packagers import that exact class</em>, with
 * a {@code TODO} saying so ("relies on internal SW details … SHRINKWRAP-192"),
 * and the behaviour that a {@code .war} entry backed by any other kind of asset
 * is silently skipped is a consequence a fixture has to be able to see.
 */
public final class Archives
{
   /** How deep {@link #tree} follows nested archives before it stops. */
   private static final int MAX_DEPTH = 3;

   private Archives()
   {
   }

   /**
    * Every path in an archive, sorted, with nested archives expanded.
    *
    * A path inside a nested archive is written {@code outer.jar!/inner/path},
    * the same way a stack trace or a jar URL writes one.
    */
   public static List<String> tree(Archive<?> archive)
   {
      List<String> out = new ArrayList<String>();
      collect(out, "", archive, 0);
      Collections.sort(out);
      return out;
   }

   /**
    * How many entries an archive holds at its own level, counted the way
    * {@link #tree} lists them — that is, without the class files javac
    * invented (see {@link Util#compilerMade}).
    *
    * {@code getContent().size()} is the obvious spelling and the wrong one: it
    * would make the same 36 archive listings move for the same non-reason, one
    * number at a time instead of one line at a time.
    */
   public static int entries(Archive<?> archive)
   {
      int n = 0;
      for (ArchivePath p : archive.getContent().keySet())
      {
         if (!Util.compilerMade(p.get()))
         {
            n++;
         }
      }
      return n;
   }

   private static void collect(List<String> out, String prefix, Archive<?> archive, int depth)
   {
      Map<ArchivePath, Node> content = archive.getContent();
      for (Map.Entry<ArchivePath, Node> e : content.entrySet())
      {
         String path = prefix + e.getKey().get();
         if (Util.compilerMade(path))
         {
            continue;
         }
         Asset asset = e.getValue().getAsset();
         if (asset instanceof ArchiveAsset && depth < MAX_DEPTH)
         {
            Archive<?> nested = ((ArchiveAsset) asset).getArchive();
            out.add(path + " (archive " + nested.getName() + ")");
            collect(out, path + "!", nested, depth + 1);
         }
         else if (asset instanceof ArchiveAsset)
         {
            out.add(path + " (archive, not followed)");
         }
         else
         {
            out.add(path);
         }
      }
   }

   /**
    * The asset at a path, as text; {@code null} if there is nothing there.
    *
    * Used for the descriptors the packagers write. Their bytes are read rather
    * than their presence asserted, because "wrote a web.xml" and "wrote the
    * right web.xml" are different claims and only one of them is worth scoring.
    */
   public static String text(Archive<?> archive, String path)
   {
      Node node = archive.get(path);
      if (node == null || node.getAsset() == null)
      {
         return null;
      }
      try
      {
         InputStream in = node.getAsset().openStream();
         try
         {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0)
            {
               bytes.write(buf, 0, n);
            }
            return new String(bytes.toByteArray(), "UTF-8");
         }
         finally
         {
            in.close();
         }
      }
      catch (Exception e)
      {
         throw new IllegalStateException("could not read " + path, e);
      }
   }

   /** The single nested archive at a path, for fixtures that want to look inside one. */
   public static Archive<?> nested(Archive<?> archive, String path)
   {
      Node node = archive.get(path);
      if (node == null || !(node.getAsset() instanceof ArchiveAsset))
      {
         return null;
      }
      return ((ArchiveAsset) node.getAsset()).getArchive();
   }
}
