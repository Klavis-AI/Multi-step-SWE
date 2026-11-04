package arqprobe;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * Transcript recorder. Everything a fixture observes is written here and
 * normalized on the way in, so a recording made against the baseline and a
 * replay made against a migrated tree are comparable byte for byte.
 */
public class Rec
{
   private final StringBuilder sb = new StringBuilder();

   public void line(String s)
   {
      sb.append(norm(s)).append('\n');
   }

   public void kv(String key, Object value)
   {
      sb.append(key).append('=').append(norm(str(value))).append('\n');
   }

   /** Values in the given order; use for sequences whose order is part of the contract. */
   public void list(String key, Collection<?> values)
   {
      int i = 0;
      for (Object v : values)
      {
         sb.append(key).append('[').append(i++).append(']').append('=')
           .append(norm(str(v))).append('\n');
      }
      sb.append(key).append(".size=").append(i).append('\n');
   }

   /** Values sorted; use where the source order is an implementation detail. */
   public void sorted(String key, Collection<?> values)
   {
      List<String> out = new ArrayList<String>();
      for (Object v : values)
      {
         out.add(norm(str(v)));
      }
      Collections.sort(out);
      list(key, out);
   }

   public void map(String key, Map<?, ?> m)
   {
      Map<String, String> sortedMap = new TreeMap<String, String>();
      for (Map.Entry<?, ?> e : m.entrySet())
      {
         sortedMap.put(norm(str(e.getKey())), norm(str(e.getValue())));
      }
      for (Map.Entry<String, String> e : sortedMap.entrySet())
      {
         sb.append(key).append('{').append(e.getKey()).append("}=")
           .append(e.getValue()).append('\n');
      }
      sb.append(key).append(".size=").append(sortedMap.size()).append('\n');
   }

   /**
    * Record a throwable as its type and message only. Stack frames carry line
    * numbers, which a migration legitimately moves; the type and the message are
    * the observable contract.
    */
   public void thrown(String key, Throwable t)
   {
      if (t == null)
      {
         sb.append(key).append("=<none>\n");
         return;
      }
      List<String[]> links = chainOf(t);
      for (int i = 0; i < links.size(); i++)
      {
         sb.append(key).append('.').append(i).append(".type=")
           .append(links.get(i)[0]).append('\n');
         sb.append(key).append('.').append(i).append(".message=")
           .append(norm(links.get(i)[1])).append('\n');
      }
   }

   /**
    * A throwable's cause chain as types only, with every message dropped.
    *
    * For the handful of outcomes whose text is the JVM's sentence about its own
    * machinery rather than anything this codebase composed. Two of those were
    * reworded between JDK 8 and 25 — the reflective access check went from
    * {@code Class X can not access ... with modifiers ""} to
    * {@code class X cannot access ... with package access}, and
    * {@code Integer.parseInt(null)} went from {@code null} to
    * {@code Cannot parse null string} — so a fixture that recorded them would
    * expire partway up the ladder while telling us nothing about the tree.
    *
    * What such a fixture is really asking is "does this fail, and how" — the
    * type chain answers that and does not move. Used deliberately and narrowly:
    * a message the codebase composes is always recorded in full.
    */
   public void thrownTypes(String key, Throwable t)
   {
      if (t == null)
      {
         sb.append(key).append("=<none>\n");
         return;
      }
      List<String> types = new ArrayList<String>();
      for (String[] link : chainOf(t))
      {
         types.add(link[0]);
      }
      list(key, types);
   }

   /**
    * A throwable whose own message this codebase composed and whose cause's
    * message the JVM did: the first link in full, the rest as types.
    *
    * Between {@link #thrown} and {@link #thrownTypes} there is a third shape,
    * and the servlet method executor is it. The executor wraps whatever went
    * wrong in {@code "Error launching test <class> <method>"} — a sentence it
    * composes, an observable it owns, and one that does not move. Underneath is
    * whatever the JVM had to say about its own machinery, which does:
    * {@code java.net.ConnectException} reports a refused loopback connect as
    * {@code Connection refused (Connection refused)} on JDK 8 and 11 and as
    * {@code Connection refused} from JDK 17 on, the doubled form being the old
    * socket implementation echoing {@code strerror}. Measured across the ladder,
    * not assumed — and applied at this one site rather than to everything that
    * throws, because the sweep says the executor's other causes
    * ({@code EOFException}, {@code StreamCorruptedException}'s header dump, even
    * {@code sun.net.www.protocol.file.FileURLConnection}'s name) hold still from
    * 8 through 25 and are recorded in full.
    *
    * <p>The suppressed links keep their line and say so, so a transcript never
    * hides that a decision was made about it.
    */
   public void thrownOuterMessage(String key, Throwable t)
   {
      if (t == null)
      {
         sb.append(key).append("=<none>\n");
         return;
      }
      List<String[]> links = chainOf(t);
      for (int i = 0; i < links.size(); i++)
      {
         sb.append(key).append('.').append(i).append(".type=")
           .append(links.get(i)[0]).append('\n');
         sb.append(key).append('.').append(i).append(".message=")
           .append(i == 0 ? norm(links.get(i)[1]) : "<jvm-composed>").append('\n');
      }
   }

   /** {@link #value}, but recording only the type chain of a throwable. */
   public void valueTypeOnly(String key, Call c)
   {
      Object v;
      try
      {
         v = c.call();
      }
      catch (Throwable t)
      {
         sb.append(key).append("=<threw>\n");
         thrownTypes(key + ".ex", t);
         return;
      }
      kv(key, v);
   }

   /**
    * The cause chain as far as the codebase owns it, outermost first.
    *
    * Two things underneath the codebase's own throwable belong to the JDK and
    * not to any contract: the names of its internal classes, and how many times
    * it wraps its own diagnosis. JAXP reports a bad UTF-8 byte as
    * {@code DescriptorImportException -> MalformedByteSequenceException} on
    * JDK 8 and, from JDK 11 on, inserts a {@code SAXParseException} carrying the
    * identical message in between. So an internal type name collapses to one
    * token, and a link that only restates the message of the link beneath it is
    * dropped. A link that says something new always survives, whoever owns it.
    *
    * @return one {@code {type, message}} pair per surviving link
    */
   public static List<String[]> chainOf(Throwable t)
   {
      List<String[]> links = new ArrayList<String[]>();
      int depth = 0;
      for (Throwable c = t; c != null && depth < 8; c = c.getCause(), depth++)
      {
         links.add(new String[] {typeOf(c), message(c)});
         if (c.getCause() == c)
         {
            break;
         }
      }
      List<String[]> out = new ArrayList<String[]>();
      for (int i = 0; i < links.size(); i++)
      {
         if (i + 1 < links.size() && links.get(i)[1].equals(links.get(i + 1)[1]))
         {
            continue;
         }
         out.add(links.get(i));
      }
      return out;
   }

   private static String typeOf(Throwable t)
   {
      String n = t.getClass().getName();
      if (n.startsWith("com.sun.") || n.startsWith("sun.")
            || n.startsWith("jdk.internal.") || n.startsWith("jdk.xml."))
      {
         return "<jdk-internal>";
      }
      return n;
   }

   /** Run a body, recording either its normal completion or the throwable it raised. */
   public void attempt(String key, Body body)
   {
      try
      {
         body.run();
         sb.append(key).append("=ok\n");
      }
      catch (Throwable t)
      {
         sb.append(key).append("=threw\n");
         thrown(key + ".ex", t);
      }
   }

   public interface Body
   {
      void run() throws Throwable;
   }

   public interface Call
   {
      Object call() throws Throwable;
   }

   /** Record an accessor's value, or the throwable it raised instead. */
   public void value(String key, Call c)
   {
      Object v;
      try
      {
         v = c.call();
      }
      catch (Throwable t)
      {
         sb.append(key).append("=<threw>\n");
         thrown(key + ".ex", t);
         return;
      }
      kv(key, v);
   }

   /** Record an accessor returning a collection, or the throwable it raised. */
   public void valueList(String key, Call c)
   {
      Object v;
      try
      {
         v = c.call();
      }
      catch (Throwable t)
      {
         sb.append(key).append("=<threw>\n");
         thrown(key + ".ex", t);
         return;
      }
      if (v == null)
      {
         sb.append(key).append("=<null>\n");
         return;
      }
      list(key, (Collection<?>) v);
   }

   /** Record an accessor returning a map, or the throwable it raised. */
   public void valueMap(String key, Call c)
   {
      Object v;
      try
      {
         v = c.call();
      }
      catch (Throwable t)
      {
         sb.append(key).append("=<threw>\n");
         thrown(key + ".ex", t);
         return;
      }
      if (v == null)
      {
         sb.append(key).append("=<null>\n");
         return;
      }
      map(key, (Map<?, ?>) v);
   }

   /**
    * Record whether an accessor returned something, returned null, or threw —
    * without recording the value itself — and hand the value back to the caller
    * so it can be walked. Use where the value is a structure, not a scalar.
    */
   public Object call(String key, Call c)
   {
      try
      {
         Object v = c.call();
         kv(key, v == null ? "<null>" : "ok");
         return v;
      }
      catch (Throwable t)
      {
         kv(key, "threw");
         thrown(key + ".ex", t);
         return null;
      }
   }

   /** {@link #call} for a list-returning accessor: records its size, returns it (empty on failure). */
   @SuppressWarnings("unchecked")
   public <T> List<T> listOf(String key, Call c)
   {
      Object v;
      try
      {
         v = c.call();
      }
      catch (Throwable t)
      {
         kv(key + ".size", "<threw>");
         thrown(key + ".ex", t);
         return Collections.emptyList();
      }
      if (v == null)
      {
         kv(key + ".size", "<null>");
         return Collections.emptyList();
      }
      List<T> list = (List<T>) v;
      kv(key + ".size", Integer.valueOf(list.size()));
      return list;
   }

   /**
    * A throwable's message as the codebase means it.
    *
    * A {@link NullPointerException} raised by a dereference carries no message
    * before JDK 15 and a JVM-composed description of the failing dereference
    * after it (JEP 358). No main source in this codebase ever constructs a
    * NullPointerException, so that text is the JVM's and not an observable the
    * codebase owns; it is collapsed so the two eras read alike.
    *
    * Trailing blanks go the same way: {@code DOMException}'s text ended in a
    * space on JDK 8 and stopped doing so later, and no message this codebase
    * composes ends in one on purpose. Whitespace that a fixture asserts about
    * lives in values, not in messages, and is left alone there.
    */
   public static String message(Throwable t)
   {
      if (t instanceof NullPointerException)
      {
         return "<jvm-npe>";
      }
      String m = t.getMessage();
      if (m == null)
      {
         return "<null>";
      }
      int end = m.length();
      while (end > 0 && (m.charAt(end - 1) == ' ' || m.charAt(end - 1) == '\t'))
      {
         end--;
      }
      return m.substring(0, end);
   }

   public String text()
   {
      return sb.toString();
   }

   private static String str(Object o)
   {
      if (o == null)
      {
         return "<null>";
      }
      if (o instanceof byte[])
      {
         return hex((byte[]) o);
      }
      if (o instanceof Object[])
      {
         return Arrays.deepToString((Object[]) o);
      }
      if (o instanceof Class)
      {
         return ((Class<?>) o).getName();
      }
      return String.valueOf(o);
   }

   public static String hex(byte[] b)
   {
      StringBuilder h = new StringBuilder();
      for (int i = 0; i < b.length; i++)
      {
         h.append(Character.forDigit((b[i] >> 4) & 0xf, 16));
         h.append(Character.forDigit(b[i] & 0xf, 16));
      }
      return h.toString();
   }

   /**
    * Normalization. Applied identically at record and replay time.
    *
    * <ul>
    * <li>identity hash codes ({@code Object@1a2b3c}, {@code Object@1a2b3c}) -> {@code @ID}</li>
    * <li>temporary directory paths -> {@code <TMP>}</li>
    * <li>the workspace root -> {@code <APP>}</li>
    * <li>the local loopback port the scorer picked -> {@code <PORT>}</li>
    * <li>CR/LF -> LF</li>
    * </ul>
    */
   public static String norm(String s)
   {
      if (s == null)
      {
         return "<null>";
      }
      String out = s.replace("\r\n", "\n").replace("\r", "\n");
      out = out.replaceAll("@[0-9a-f]{4,}", "@ID");
      out = out.replaceAll("/tmp/[A-Za-z0-9_.-]+", "<TMP>");
      out = out.replaceAll("/app[A-Za-z0-9_./-]*", "<APP>");
      out = out.replaceAll("localhost:[0-9]{2,5}", "localhost:<PORT>");
      out = out.replaceAll("127\\.0\\.0\\.1:[0-9]{2,5}", "127.0.0.1:<PORT>");
      return out;
   }
}
