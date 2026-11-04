package arqprobe.fam;

import java.util.ArrayList;
import java.util.List;

/**
 * Whitespace-insensitive view of an XML document: the ordered stream of start
 * tags (with sorted attributes), text runs and end tags. Two documents with the
 * same stream are the same document as far as any consumer is concerned, which
 * lets a recording survive a change of XML writer without surviving a change of
 * content.
 */
public final class Xml
{
   private Xml()
   {
   }

   public static List<String> tagStream(String xml)
   {
      List<String> out = new ArrayList<String>();
      int i = 0;
      int n = xml.length();
      while (i < n)
      {
         int lt = xml.indexOf('<', i);
         if (lt < 0)
         {
            addText(out, xml.substring(i));
            break;
         }
         addText(out, xml.substring(i, lt));
         int gt = xml.indexOf('>', lt);
         if (gt < 0)
         {
            out.add("TRUNCATED");
            break;
         }
         String tag = xml.substring(lt + 1, gt).trim();
         if (tag.startsWith("?") || tag.startsWith("!"))
         {
            out.add("PI " + squeeze(tag));
         }
         else if (tag.startsWith("/"))
         {
            out.add("END " + tag.substring(1).trim());
         }
         else
         {
            boolean empty = tag.endsWith("/");
            if (empty)
            {
               tag = tag.substring(0, tag.length() - 1).trim();
            }
            out.add((empty ? "EMPTY " : "START ") + normalizeStart(tag));
         }
         i = gt + 1;
      }
      return out;
   }

   private static void addText(List<String> out, String text)
   {
      String t = text.trim();
      if (t.length() > 0)
      {
         out.add("TEXT " + squeeze(t));
      }
   }

   private static String squeeze(String s)
   {
      return s.replaceAll("\\s+", " ").trim();
   }

   private static String normalizeStart(String tag)
   {
      String t = squeeze(tag);
      int sp = t.indexOf(' ');
      if (sp < 0)
      {
         return t;
      }
      String name = t.substring(0, sp);
      List<String> attrs = new ArrayList<String>();
      String rest = t.substring(sp + 1);
      int i = 0;
      while (i < rest.length())
      {
         int eq = rest.indexOf('=', i);
         if (eq < 0)
         {
            break;
         }
         String key = rest.substring(i, eq).trim();
         int q = eq + 1;
         while (q < rest.length() && rest.charAt(q) == ' ')
         {
            q++;
         }
         if (q >= rest.length())
         {
            break;
         }
         char quote = rest.charAt(q);
         int end = rest.indexOf(quote, q + 1);
         if (end < 0)
         {
            break;
         }
         attrs.add(key + "=" + rest.substring(q + 1, end));
         i = end + 1;
      }
      java.util.Collections.sort(attrs);
      StringBuilder sb = new StringBuilder(name);
      for (String a : attrs)
      {
         sb.append(' ').append(a);
      }
      return sb.toString();
   }
}
