package arqgate;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;
import java.lang.module.Configuration;
import java.lang.module.FindException;
import java.lang.module.ModuleFinder;
import java.lang.module.ModuleReference;
import java.lang.module.ResolutionException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.jar.Attributes;
import java.util.jar.Manifest;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * The module gate: what the published artifacts call themselves, whether the
 * platform will read them as modules at all, and whether they resolve together.
 *
 * <pre>
 *   java -cp arqmodules.jar arqgate.Modules &lt;out.txt&gt; &lt;jarlist.txt&gt;
 * </pre>
 *
 * <p>{@code jarlist.txt} is one jar path per line -- the scorer passes the same
 * list of published module jars the other gates grade, which is already
 * filtered to artifacts with no classifier. That filtering is load-bearing and
 * not tidiness: a {@code -sources.jar} contains {@code .java} files and no
 * packages of classes, so it derives no descriptor and belongs on no module
 * path, and a gate that globbed {@code *.jar} would report seven failures
 * against a tree that has none.</p>
 *
 * <p>Three questions are asked separately because a tree can answer one and not
 * the next. A jar can carry {@code Automatic-Module-Name} and still not be
 * readable as a module -- that is what an illegal {@code META-INF/services}
 * entry does. A set of jars can each be readable and still not resolve
 * together -- that is what a duplicate name or a split package does.</p>
 *
 * <p>Every question is asked through {@code java.lang.module} rather than by
 * running the {@code jar} tool and reading its output. {@code jar
 * --describe-module} prints {@code Unable to derive module descriptor for:} to
 * stdout and <em>exits 0</em>, so a screen written the obvious way -- run it,
 * check the status -- reports every jar as fine. This class asks the API, which
 * throws.</p>
 */
public final class Modules
{
   /** What a jar is called, and on whose authority. */
   private static final class Named
   {
      final String artifact;
      String name = "";
      /** {@code manifest}, {@code derived}, or {@code none}. */
      String source = "none";
      String reason = "";

      Named(String artifact)
      {
         this.artifact = artifact;
      }
   }

   /**
    * The coordinate part of a published jar name, with the version and any
    * classifier removed: arquillian-api-1.0.0-SNAPSHOT.jar -> arquillian-api.
    * The baseline of expected names is keyed this way so that it survives a
    * version bump, which is a thing later rungs do.
    */
   private static String artifactOf(Path jar)
   {
      String n = jar.getFileName().toString();
      if (n.endsWith(".jar"))
      {
         n = n.substring(0, n.length() - 4);
      }
      int v = n.indexOf("-1.0.0-SNAPSHOT");
      return v < 0 ? n : n.substring(0, v);
   }

   /**
    * The manifest's Automatic-Module-Name, or the empty string. Read with
    * {@link Manifest} and not by scanning lines: the manifest format wraps a
    * long value onto a continuation line beginning with a space, and every
    * name here is long enough to wrap.
    */
   private static String manifestName(Path jar) throws IOException
   {
      ZipFile zip = new ZipFile(jar.toFile());
      try
      {
         ZipEntry e = zip.getEntry("META-INF/MANIFEST.MF");
         if (e == null)
         {
            return "";
         }
         InputStream in = zip.getInputStream(e);
         try
         {
            Attributes main = new Manifest(in).getMainAttributes();
            String v = main.getValue("Automatic-Module-Name");
            return v == null ? "" : v.trim();
         }
         finally
         {
            in.close();
         }
      }
      finally
      {
         zip.close();
      }
   }

   public static void main(String[] args) throws Exception
   {
      if (args.length != 2)
      {
         System.err.println("usage: Modules <out.txt> <jarlist.txt>");
         System.exit(2);
      }
      List<Path> jars = new ArrayList<Path>();
      for (String line : Files.readAllLines(Paths.get(args[1]), StandardCharsets.UTF_8))
      {
         String t = line.trim();
         if (t.isEmpty() || t.startsWith("#"))
         {
            continue;
         }
         Path p = Paths.get(t);
         if (Files.isRegularFile(p))
         {
            jars.add(p);
         }
      }
      Collections.sort(jars);

      List<Named> named = new ArrayList<Named>();
      int declared = 0;
      int derivable = 0;
      for (Path jar : jars)
      {
         Named n = new Named(artifactOf(jar));
         String fromManifest = manifestName(jar);
         // Asked even when the manifest answers, because the manifest naming a
         // module and the platform accepting the jar as one are different
         // claims, and this step exists because a tree can pass the first and
         // fail the second.
         String derivedName = "";
         try
         {
            Set<ModuleReference> refs = ModuleFinder.of(jar).findAll();
            for (ModuleReference r : refs)
            {
               derivedName = r.descriptor().name();
            }
            derivable++;
         }
         catch (FindException e)
         {
            Throwable cause = e.getCause() == null ? e : e.getCause();
            String m = cause.getMessage();
            n.reason = m == null ? cause.getClass().getName() : m.replace(' ', '_');
         }
         if (!fromManifest.isEmpty())
         {
            declared++;
            n.name = fromManifest;
            n.source = "manifest";
         }
         else if (!derivedName.isEmpty())
         {
            n.name = derivedName;
            n.source = "derived";
         }
         named.add(n);
      }

      // All of them at once, against the boot layer. Resolution is the question
      // neither of the per-jar checks can answer: two jars that each read
      // perfectly well still cannot sit on one module path if they claim the
      // same name or split a package between them.
      int resolved = 0;
      String resolveNote = "not-attempted";
      if (!jars.isEmpty())
      {
         try
         {
            ModuleFinder finder = ModuleFinder.of(jars.toArray(new Path[0]));
            Set<String> roots = new LinkedHashSet<String>();
            for (ModuleReference r : finder.findAll())
            {
               roots.add(r.descriptor().name());
            }
            Configuration cf = ModuleLayer.boot().configuration()
               .resolve(finder, ModuleFinder.of(), roots);
            for (String r : roots)
            {
               Optional<?> m = cf.findModule(r);
               if (m.isPresent())
               {
                  resolved++;
               }
            }
            resolveNote = "ok";
         }
         catch (FindException e)
         {
            resolved = 0;
            resolveNote = note(e);
         }
         catch (ResolutionException e)
         {
            resolved = 0;
            resolveNote = note(e);
         }
      }

      PrintStream out = new PrintStream(new File(args[0]), "UTF-8");
      try
      {
         out.println("mod.total=" + jars.size());
         out.println("mod.declared=" + declared);
         out.println("mod.derivable=" + derivable);
         out.println("mod.resolved=" + resolved);
         out.println("mod.resolve=" + resolveNote);
         for (int i = 0; i < named.size(); i++)
         {
            Named n = named.get(i);
            StringBuilder sb = new StringBuilder();
            sb.append("mod[").append(i).append("]=").append(n.artifact)
              .append(" name=").append(n.name.isEmpty() ? "-" : n.name)
              .append(" source=").append(n.source);
            if (!n.reason.isEmpty())
            {
               sb.append(" reason=").append(n.reason);
            }
            out.println(sb.toString());
         }
      }
      finally
      {
         out.close();
      }
      System.out.println("modules: total=" + jars.size() + " declared=" + declared
         + " derivable=" + derivable + " resolved=" + resolved + " (" + resolveNote + ")");
   }

   /** One line, no spaces: these land in a fact file that is parsed by field. */
   private static String note(Throwable t)
   {
      String m = t.getMessage();
      if (m == null)
      {
         return t.getClass().getName();
      }
      return m.replace(' ', '_').replace('\n', '/');
   }
}
