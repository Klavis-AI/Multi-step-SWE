package arqprobe.fam.life;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.URL;
import java.net.URLClassLoader;

/**
 * The classpath a client run is booted against.
 *
 * Arquillian configures itself from files it finds through the thread context
 * class loader — {@code arquillian.xml}, {@code arquillian.launch}, and
 * {@code META-INF/services} registrations — so a fixture that wants a different
 * configuration builds a different classpath. This writes one into a scratch
 * directory and installs it for the duration of the run.
 *
 * Delegation to the probe's own loader is left alone, including for service
 * registrations: the tree's jars register a {@code Protocol}, a
 * {@code TestEnricher} and three {@code AuxiliaryArchiveAppender}s of their own,
 * and a client run that could not see them would not be a client run. The one
 * registration the overlay cannot supply is the container itself — the registry
 * looks that one up through the container's class loader, which is the probe's,
 * not this one — so {@code probe/services} carries it statically.
 */
public final class Env
{
   private final File root;

   private ClassLoader previous;

   public Env() throws IOException
   {
      File tmp = File.createTempFile("arqprobe-life", "");
      if (!tmp.delete() || !tmp.mkdirs())
      {
         throw new IOException("could not create a scratch directory");
      }
      this.root = tmp;
   }

   /** The scratch root, for fixtures that want a path to hand to the engine. */
   public File root()
   {
      return root;
   }

   public Env file(String path, String contents) throws IOException
   {
      File f = new File(root, path);
      File dir = f.getParentFile();
      if (!dir.isDirectory() && !dir.mkdirs())
      {
         throw new IOException("could not create " + dir);
      }
      OutputStream out = new FileOutputStream(f);
      try
      {
         out.write(contents.getBytes("UTF-8"));
      }
      finally
      {
         out.close();
      }
      return this;
   }

   public Env arquillianXml(String xml) throws IOException
   {
      return file("arquillian.xml", xml);
   }

   /**
    * The file that names which container configuration is active.
    *
    * Absent, the registry takes the one marked default; present, it takes the
    * one this names — and a name matching a group takes every container in it.
    */
   public Env launch(String name) throws IOException
   {
      return file("arquillian.launch", name);
   }

   public Env service(Class<?> service, Class<?>... providers) throws IOException
   {
      StringBuilder sb = new StringBuilder();
      for (int i = 0; i < providers.length; i++)
      {
         sb.append(providers[i].getName()).append('\n');
      }
      return file("META-INF/services/" + service.getName(), sb.toString());
   }

   /** Installs the overlay as the thread context class loader. */
   public Env install() throws IOException
   {
      Thread current = Thread.currentThread();
      previous = current.getContextClassLoader();
      current.setContextClassLoader(
            new URLClassLoader(new URL[] {root.toURI().toURL()}, Env.class.getClassLoader()));
      return this;
   }

   /**
    * Puts the previous loader back.
    *
    * Worth doing even though every fixture gets a fresh thread: a run that is
    * still holding the overlay when it records would record a loader identity
    * that moves with the temp directory name.
    */
   public void restore()
   {
      if (previous != null)
      {
         Thread.currentThread().setContextClassLoader(previous);
         previous = null;
      }
   }
}
