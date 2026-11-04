package arqprobe.fam.svc;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;

/**
 * Builds the {@code META-INF/services} entries a fixture wants the loaders to
 * find.
 *
 * The provider classes themselves stay on the probe's own classpath and are
 * reached by delegation; only the registration files are synthesized, so a
 * fixture controls exactly what is declared without a compile step.
 */
public final class Services
{
   private final List<File> roots = new ArrayList<File>();
   private final File base;

   public Services() throws IOException
   {
      File tmp = File.createTempFile("arqprobe-svc", "");
      if (!tmp.delete() || !tmp.mkdirs())
      {
         throw new IOException("could not create a scratch directory");
      }
      this.base = tmp;
   }

   /**
    * Declare a service file in a new classpath root, written verbatim as UTF-8.
    *
    * Each call adds one root, so two roots is how a service declared twice on
    * the classpath is produced.
    */
   public Services declare(Class<?> service, String contents) throws IOException
   {
      File root = new File(base, "root" + roots.size());
      File file = new File(root, "META-INF/services/" + service.getName());
      if (!file.getParentFile().mkdirs())
      {
         throw new IOException("could not create " + file.getParentFile().getName());
      }
      OutputStream out = new FileOutputStream(file);
      try
      {
         out.write(contents.getBytes("UTF-8"));
      }
      finally
      {
         out.close();
      }
      roots.add(root);
      return this;
   }

   /** Declares providers one per line, the ordinary case. */
   public Services declare(Class<?> service, Class<?>... providers) throws IOException
   {
      StringBuilder sb = new StringBuilder();
      for (int i = 0; i < providers.length; i++)
      {
         sb.append(providers[i].getName()).append('\n');
      }
      return declare(service, sb.toString());
   }

   /** An empty root, so the loader finds no registration at all. */
   public Services declareNothing() throws IOException
   {
      File root = new File(base, "root" + roots.size());
      if (!root.mkdirs())
      {
         throw new IOException("could not create " + root.getName());
      }
      roots.add(root);
      return this;
   }

   /**
    * A loader that finds exactly the declared services and no others.
    *
    * Classes still come from the parent by ordinary delegation, but
    * {@code META-INF/services} lookups do not: the tree's own jars register
    * {@code TestEnricher} providers of their own, and a fixture asking what the
    * loaders make of one service file must not also be reading those.
    */
   public ClassLoader loader() throws IOException
   {
      URL[] urls = new URL[roots.size()];
      for (int i = 0; i < roots.size(); i++)
      {
         urls[i] = roots.get(i).toURI().toURL();
      }
      return new URLClassLoader(urls, Services.class.getClassLoader())
      {
         @Override
         public URL getResource(String name)
         {
            return declared(name) ? findResource(name) : super.getResource(name);
         }

         @Override
         public Enumeration<URL> getResources(String name) throws IOException
         {
            return declared(name) ? findResources(name) : super.getResources(name);
         }
      };
   }

   private static boolean declared(String name)
   {
      return name != null && name.startsWith("META-INF/services/");
   }

   /** Runs a body with the built loader installed as the thread context loader. */
   public void asContextClassLoader(Runnable body) throws IOException
   {
      Thread current = Thread.currentThread();
      ClassLoader previous = current.getContextClassLoader();
      current.setContextClassLoader(loader());
      try
      {
         body.run();
      }
      finally
      {
         current.setContextClassLoader(previous);
      }
   }
}
