package arqprobe.fam.life;

import java.util.ArrayList;
import java.util.List;

/**
 * Writes the {@code arquillian.xml} a fixture wants booted.
 *
 * Deliberately a plain text builder rather than the descriptor API: what is
 * under test here is the path from a file on the classpath to a running
 * container, and starting from an object graph would skip the half of it that
 * parses. The descriptor API has its own family.
 */
public final class Conf
{
   private final List<String> engine = new ArrayList<String>();

   private final List<String> body = new ArrayList<String>();

   private String defaultProtocol;

   public static Conf arquillian()
   {
      return new Conf();
   }

   public Conf deploymentExportPath(String path)
   {
      engine.add(property("deploymentExportPath", path));
      return this;
   }

   public Conf maxTestClassesBeforeRestart(int max)
   {
      engine.add(property("maxTestClassesBeforeRestart", String.valueOf(max)));
      return this;
   }

   public Conf defaultProtocol(String type)
   {
      this.defaultProtocol = type;
      return this;
   }

   /** A container block; the returned builder is still this one. */
   public Container container(String qualifier)
   {
      return new Container(this, qualifier, null);
   }

   public Group group(String qualifier)
   {
      return new Group(this, qualifier);
   }

   public String text()
   {
      StringBuilder sb = new StringBuilder();
      sb.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
      sb.append("<arquillian>\n");
      if (!engine.isEmpty())
      {
         sb.append("   <engine>\n");
         for (String e : engine)
         {
            sb.append("      ").append(e).append('\n');
         }
         sb.append("   </engine>\n");
      }
      if (defaultProtocol != null)
      {
         // The element is <protocol>, directly under the root. The descriptor
         // reads the document rather than a schema, so an element by any other
         // name is not an error — it is silently nothing, which is how the
         // first version of this went unnoticed.
         sb.append("   <protocol type=\"").append(esc(defaultProtocol)).append("\"/>\n");
      }
      for (String b : body)
      {
         sb.append(b);
      }
      sb.append("</arquillian>\n");
      return sb.toString();
   }

   static String property(String name, String value)
   {
      return "<property name=\"" + esc(name) + "\">" + esc(value) + "</property>";
   }

   static String esc(String s)
   {
      return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
              .replace("\"", "&quot;");
   }

   /** A {@code <container>} block, optionally nested inside a group. */
   public static final class Container
   {
      private final Conf owner;

      private final Group group;

      private final String qualifier;

      private final List<String> configuration = new ArrayList<String>();

      private final List<String> protocols = new ArrayList<String>();

      private boolean isDefault;

      Container(Conf owner, String qualifier, Group group)
      {
         this.owner = owner;
         this.qualifier = qualifier;
         this.group = group;
      }

      public Container asDefault()
      {
         this.isDefault = true;
         return this;
      }

      public Container property(String name, String value)
      {
         configuration.add(Conf.property(name, value));
         return this;
      }

      /** Names the container to itself, which is how its callbacks identify it. */
      public Container named()
      {
         return property("name", qualifier);
      }

      public Container protocol(String type, String... nameValues)
      {
         StringBuilder sb = new StringBuilder();
         sb.append("<protocol type=\"").append(esc(type)).append("\">");
         if (nameValues.length > 0)
         {
            // Properties live under a <configuration> element, here as in a
            // container block; without it they are read as nothing at all.
            sb.append("<configuration>");
            for (int i = 0; i + 1 < nameValues.length; i += 2)
            {
               sb.append(Conf.property(nameValues[i], nameValues[i + 1]));
            }
            sb.append("</configuration>");
         }
         sb.append("</protocol>");
         protocols.add(sb.toString());
         return this;
      }

      /** Closes the block and returns the document builder. */
      public Conf end()
      {
         String indent = group == null ? "   " : "      ";
         StringBuilder sb = new StringBuilder();
         sb.append(indent).append("<container qualifier=\"").append(esc(qualifier)).append('"');
         if (isDefault)
         {
            sb.append(" default=\"true\"");
         }
         sb.append(">\n");
         if (!configuration.isEmpty())
         {
            sb.append(indent).append("   <configuration>\n");
            for (String c : configuration)
            {
               sb.append(indent).append("      ").append(c).append('\n');
            }
            sb.append(indent).append("   </configuration>\n");
         }
         for (String p : protocols)
         {
            sb.append(indent).append("   ").append(p).append('\n');
         }
         sb.append(indent).append("</container>\n");
         if (group == null)
         {
            owner.body.add(sb.toString());
         }
         else
         {
            group.containers.add(sb.toString());
         }
         return owner;
      }
   }

   /** A {@code <group>} block. */
   public static final class Group
   {
      private final Conf owner;

      private final String qualifier;

      private final List<String> containers = new ArrayList<String>();

      Group(Conf owner, String qualifier)
      {
         this.owner = owner;
         this.qualifier = qualifier;
      }

      public Container container(String name)
      {
         return new Container(owner, name, this);
      }

      public Conf end()
      {
         StringBuilder sb = new StringBuilder();
         sb.append("   <group qualifier=\"").append(esc(qualifier)).append("\">\n");
         for (String c : containers)
         {
            sb.append(c);
         }
         sb.append("   </group>\n");
         owner.body.add(sb.toString());
         return owner;
      }
   }
}
