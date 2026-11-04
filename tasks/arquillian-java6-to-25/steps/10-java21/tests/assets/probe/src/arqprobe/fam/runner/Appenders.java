package arqprobe.fam.runner;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.TreeSet;

import arqprobe.Rec;
import arqprobe.Util;
import arqprobe.fam.servlet.Archives;

import org.jboss.arquillian.spi.client.deployment.AuxiliaryArchiveAppender;
import org.jboss.shrinkwrap.api.Archive;
import org.jboss.shrinkwrap.api.ArchivePath;

/**
 * Reading back what a framework integration's auxiliary archive appender made.
 *
 * <p><b>Why the obvious recording is the wrong one.</b> Both appenders call
 * {@code addPackages(true, ...)} on the test framework's own packages, so the
 * archive they produce is mostly JUnit's or TestNG's class files. Listing them
 * would produce a two-thousand-line transcript that says nothing about this
 * codebase and changes wholesale on any framework bump — an expectation with no
 * information in it that also expires. The paths are read, but what is written
 * down is the set of <em>decisions</em> visible in them:
 *
 * <ul>
 * <li>the archive's name and type, which the appender chose;
 * <li>the service registration, read as bytes — this is how the container-side
 *     lookup finds the runner at all, and it is entirely the tree's own text;
 * <li>the integration's own classes, one at a time, because those are the
 *     classes this codebase owns and their absence would be a real regression;
 * <li>counts of the paths each {@code Filters.exclude(...)} was written to
 *     remove, which is the only way an exclusion that stopped matching would
 *     ever show up — a filter that silently matches nothing leaves an archive
 *     that is merely bigger, and nothing else says so;
 * <li>the depth-1 directory set, which is coarse enough to survive a patch bump
 *     of the framework and specific enough to change when the framework itself
 *     is replaced.
 * </ul>
 *
 * <p>The last one is the only entry here that a framework upgrade is expected to
 * move, and it is kept for exactly that reason: it is the observable that makes
 * "the deployment now carries JUnit 5 instead of JUnit 4" a thing the suite
 * notices rather than a thing it is blind to.
 */
public final class Appenders
{
   private Appenders()
   {
   }

   /**
    * One appender, recorded.
    *
    * @param owned   prefix of the integration's own package, as a path
    * @param markers individual paths to ask about, present or absent
    * @param filtered path fragments the appender's filters were written to
    *                 exclude; the count of matching entries is recorded, and the
    *                 interesting value is zero
    */
   public static void record(Rec r, AuxiliaryArchiveAppender appender, String owned,
         String[] markers, String[] filtered)
   {
      r.kv("appender", appender.getClass().getName());
      Archive<?> archive = appender.createAuxiliaryArchive();
      r.kv("archive.name", archive.getName());
      r.kv("archive.type", type(archive));

      List<String> paths = new ArrayList<String>();
      for (ArchivePath p : archive.getContent().keySet())
      {
         paths.add(p.get());
      }
      Collections.sort(paths);

      // The service file is the whole point of the archive: without it the
      // container-side TestRunners lookup finds no provider and the deployed
      // test cannot run. Recorded as text, not as presence.
      String service = Archives.text(archive, "/META-INF/services/org.jboss.arquillian.spi.TestRunner");
      r.kv("service.registration", service);

      for (String marker : markers)
      {
         r.kv("has " + marker, Boolean.valueOf(paths.contains(marker)));
      }

      // Every class this codebase declares, listed in full — each one is a
      // class the integration would stop working without.
      //
      // Declares, not compiles: Util.compilerMade drops Arquillian$1.class and
      // its eight siblings. Those exist because somebody wrote an anonymous
      // class, and they stop existing when somebody writes a lambda instead,
      // which is a thing this ladder requires from Java 8 onwards. What is left
      // is still the whole census of author-named types -- Arquillian,
      // Arquillian$EmptyStatement, Arquillian$StatementLifecycleExecutor,
      // JUnitDeploymentAppender, JUnitTestRunner$ExpectedExceptionHolder,
      // JUnitTestRunner -- and an appender that drops one of those still fails.
      List<String> own = new ArrayList<String>();
      for (String path : paths)
      {
         if (path.startsWith(owned) && path.endsWith(".class") && !Util.compilerMade(path))
         {
            own.add(path);
         }
      }
      r.sorted("owned.classes", own);

      for (String fragment : filtered)
      {
         int n = 0;
         for (String path : paths)
         {
            if (path.indexOf(fragment) >= 0)
            {
               n++;
            }
         }
         r.kv("filtered " + fragment, Integer.valueOf(n));
      }

      TreeSet<String> top = new TreeSet<String>();
      for (String path : paths)
      {
         int slash = path.indexOf('/', 1);
         top.add(slash < 0 ? path : path.substring(0, slash));
      }
      r.sorted("top.level", top);
   }

   /** The archive's ShrinkWrap type, without ShrinkWrap's package. */
   private static String type(Archive<?> archive)
   {
      for (Class<?> i : archive.getClass().getInterfaces())
      {
         if (i.getName().startsWith("org.jboss.shrinkwrap.api.spec."))
         {
            return i.getSimpleName();
         }
      }
      return archive.getClass().getName();
   }
}
