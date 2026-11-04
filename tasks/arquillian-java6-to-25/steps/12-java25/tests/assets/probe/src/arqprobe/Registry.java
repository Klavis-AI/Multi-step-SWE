package arqprobe;

import java.io.File;
import java.lang.reflect.Constructor;

/**
 * Maps a fixture id to the {@link Fixture} that runs it.
 *
 * Ids are {@code <family>/<name>}. The family resolves by convention to
 * {@code arqprobe.fam.<Family>Fixture}, constructed with the fixture name and
 * the family's input directory; what the fixture does with either is its own
 * business — data-driven families read a file, code-driven families switch on
 * the name.
 */
public final class Registry
{
   private Registry()
   {
   }

   public static Fixture create(String id, File inputs) throws Exception
   {
      int slash = id.indexOf('/');
      if (slash < 0)
      {
         throw new IllegalArgumentException("fixture id must be <family>/<name>: " + id);
      }
      String family = id.substring(0, slash);
      String name = id.substring(slash + 1);
      String cls = "arqprobe.fam."
            + Character.toUpperCase(family.charAt(0)) + family.substring(1) + "Fixture";
      Constructor<?> c = Class.forName(cls).getConstructor(String.class, File.class);
      return (Fixture) c.newInstance(name, new File(inputs, family));
   }
}
