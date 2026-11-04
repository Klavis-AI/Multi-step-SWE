package arqprobe.fam;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.Util;

import org.jboss.arquillian.impl.configuration.api.ArquillianDescriptor;
import org.jboss.arquillian.impl.configuration.api.ContainerDef;
import org.jboss.arquillian.impl.configuration.api.DefaultProtocolDef;
import org.jboss.arquillian.impl.configuration.api.ExtensionDef;
import org.jboss.arquillian.impl.configuration.api.GroupDef;
import org.jboss.arquillian.impl.configuration.api.ProtocolDef;
import org.jboss.shrinkwrap.descriptor.api.Descriptors;
import org.jboss.shrinkwrap.descriptor.api.Node;
import org.jboss.shrinkwrap.descriptor.spi.NodeProvider;

/**
 * Imports one {@code arquillian.xml} document through the descriptor importer
 * and records the whole resulting model, the re-export, and the raw node tree —
 * or, for a document the importer rejects or a model accessor that blows up on
 * it, the throwable that came out.
 *
 * Every accessor is recorded through {@link Rec#value} so that a document which
 * imports but cannot be read yields a transcript, not a harness failure.
 */
public class DescFixture implements Fixture
{
   private final File input;

   public DescFixture(String name, File dir)
   {
      this.input = new File(dir, name + ".xml");
   }

   public void run(Rec r) throws Throwable
   {
      byte[] bytes = Util.readAll(new FileInputStream(input));
      r.kv("input.bytes", Integer.valueOf(bytes.length));

      ArquillianDescriptor d;
      try
      {
         d = Descriptors.importAs(ArquillianDescriptor.class)
               .from(new ByteArrayInputStream(bytes));
      }
      catch (Throwable t)
      {
         r.kv("import", "threw");
         r.thrown("import.ex", t);
         return;
      }
      r.kv("import", "ok");
      r.kv("import.class", d == null ? null : d.getClass().getName());
      if (d == null)
      {
         return;
      }
      dumpModel(r, d);
      dumpExport(r, d);
      dumpNodes(r, d);
   }

   static void dumpModel(Rec r, final ArquillianDescriptor d)
   {
      r.value("engine.deploymentExportPath", () -> d.engine().getDeploymentExportPath());
      r.value("engine.maxTestClassesBeforeRestart", () -> d.engine().getMaxTestClassesBeforeRestart());

      final DefaultProtocolDef dp =
            (DefaultProtocolDef) r.call("defaultProtocol.get", () -> d.getDefaultProtocol());
      if (dp != null)
      {
         r.value("defaultProtocol.type", () -> dp.getType());
         r.valueMap("defaultProtocol.props", () -> dp.getProperties());
      }

      List<ContainerDef> containers = r.listOf("containers", () -> d.getContainers());
      for (int i = 0; i < containers.size(); i++)
      {
         dumpContainer(r, "container[" + i + "]", containers.get(i), true);
      }

      List<GroupDef> groups = r.listOf("groups", () -> d.getGroups());
      for (int i = 0; i < groups.size(); i++)
      {
         final GroupDef g = groups.get(i);
         String p = "group[" + i + "]";
         r.value(p + ".name", () -> g.getGroupName());
         List<ContainerDef> gc = r.listOf(p + ".containers", () -> g.getGroupContainers());
         for (int j = 0; j < gc.size(); j++)
         {
            dumpContainer(r, p + ".container[" + j + "]", gc.get(j), false);
         }
      }

      List<ExtensionDef> exts = r.listOf("extensions", () -> d.getExtensions());
      for (int i = 0; i < exts.size(); i++)
      {
         final ExtensionDef e = exts.get(i);
         r.value("extension[" + i + "].name", () -> e.getExtensionName());
         r.valueMap("extension[" + i + "].props", () -> e.getExtensionProperties());
      }
   }

   private static void dumpContainer(Rec r, String p, final ContainerDef c, boolean withProtocols)
   {
      r.value(p + ".name", () -> c.getContainerName());
      r.value(p + ".default", () -> Boolean.valueOf(c.isDefault()));
      r.valueMap(p + ".props", () -> c.getContainerProperties());
      r.valueList(p + ".deps", () -> c.getDependencies());
      if (!withProtocols)
      {
         return;
      }
      List<ProtocolDef> protos = r.listOf(p + ".protocols", () -> c.getProtocols());
      for (int j = 0; j < protos.size(); j++)
      {
         final ProtocolDef pd = protos.get(j);
         r.value(p + ".protocol[" + j + "].type", () -> pd.getType());
         r.valueMap(p + ".protocol[" + j + "].props", () -> pd.getProtocolProperties());
      }
   }

   private static void dumpExport(Rec r, final ArquillianDescriptor d)
   {
      // Compare the tag stream rather than the bytes: indentation is a property
      // of the XML writer, content is a property of the model.
      Object xml = r.call("export", () -> Xml.tagStream(d.exportAsString()));
      if (xml instanceof List)
      {
         r.list("export.tags", (List<?>) xml);
      }
   }

   private static void dumpNodes(Rec r, final ArquillianDescriptor d)
   {
      Object nodes = r.call("nodes", () -> {
         List<String> out = new ArrayList<String>();
         walk(((NodeProvider) d).getRootNode(), "", out);
         return out;
      });
      if (nodes instanceof List)
      {
         r.list("node", (List<?>) nodes);
      }
   }

   private static void walk(Node n, String path, List<String> out)
   {
      String here = path + "/" + n.name();
      StringBuilder sb = new StringBuilder(here);
      Map<String, String> attrs = n.attributes();
      List<String> keys = new ArrayList<String>(attrs.keySet());
      Collections.sort(keys);
      for (String k : keys)
      {
         sb.append(" @").append(k).append('=').append(attrs.get(k));
      }
      if (n.text() != null)
      {
         sb.append(" #").append(n.text());
      }
      out.add(sb.toString());
      for (Node child : n.children())
      {
         walk(child, here, out);
      }
   }

}
