package arqprobe.fam.servlet;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.ObjectOutputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpHandler;
import com.sun.net.httpserver.HttpServer;

/**
 * A real HTTP server on loopback, because the executor's observable is a
 * request.
 *
 * {@code ServletMethodExecutor} returns a {@code TestResult} and throws; that is
 * all a caller can see of it. Everything else it does — the URL it composed, the
 * query parameters it chose, whether it retried a 404, whether it followed a
 * redirect — happens on the wire. Half of what a migration could break lives
 * there: a rewritten client that dropped {@code outputMode=serializedObject},
 * or stopped retrying, or started following redirects it used to refuse, would
 * still return a {@code TestResult} on the happy path. So the fixture stands up
 * a server, serves scripted replies, and records what arrived.
 *
 * <p><b>Why {@code com.sun.net.httpserver} and not a socket.</b> It is a JDK
 * API, present and unrenamed from 6 through 25, and it is not part of the tree
 * under test — so using it neither reaches past the codebase's surface nor
 * pins the harness to a servlet container. A hand-rolled socket would have to
 * reimplement chunked encoding and {@code Connection: close} correctly to avoid
 * teaching the transcripts about my own bugs.
 *
 * <p><b>Ports are normalized, request counts mostly are not.</b> {@code Rec.norm}
 * rewrites {@code 127.0.0.1:<port>} so a transcript does not depend on which
 * ephemeral port the kernel handed out. A retry <em>count</em> is different: the
 * executor loops for a wall-clock second sleeping 200 ms per miss, so on a busy
 * machine it makes five requests or it makes four. Fixtures that can pin the
 * count do (the responder stops missing after a fixed number); the one that
 * cannot records only that a retry happened. See {@code logFirstOnly}.
 */
public final class Http
{
   /** What to answer; {@code seq} is 1 for the first request, 2 for the second. */
   public interface Responder
   {
      Reply reply(int seq) throws IOException;
   }

   /** One scripted response. */
   public static final class Reply
   {
      final int status;

      final byte[] body;

      final String location;

      public Reply(int status, byte[] body)
      {
         this(status, body, null);
      }

      public Reply(int status, byte[] body, String location)
      {
         this.status = status;
         this.body = body == null ? new byte[0] : body;
         this.location = location;
      }
   }

   private final HttpServer server;

   private final int port;

   private int requests;

   /** Requests as the server saw them, in order; drained by the fixture. */
   private final List<String> seen = Collections.synchronizedList(new ArrayList<String>());

   /**
    * Whether every request is written to the log or only the first.
    *
    * The request line is worth recording — it carries the query string the
    * executor composed — but a fixture whose request count is a function of the
    * clock must not write a line per request, or its transcript length becomes
    * an expectation that expires. Those fixtures log the first request, which is
    * the one that carries the query, and record the retry as a boolean.
    */
   private final boolean logFirstOnly;

   private Http(boolean logFirstOnly, final Responder responder) throws IOException
   {
      this.logFirstOnly = logFirstOnly;
      this.server = HttpServer.create(
            new InetSocketAddress(InetAddress.getByName("127.0.0.1"), 0), 0);
      this.server.createContext("/", new HttpHandler()
      {
         public void handle(HttpExchange exchange) throws IOException
         {
            int seq;
            synchronized (Http.this)
            {
               seq = ++requests;
            }
            if (!logFirstOnly || seq == 1)
            {
               // Not Log.add: Log is a ThreadLocal and this is the server's
               // thread, not the fixture's. A request logged here would be
               // written to a list nobody reads and the transcript would show
               // an empty log while the exchange plainly happened. The fixture
               // drains seen() onto its own log once the server has stopped.
               // The query is recorded raw and separately, and a missing one is
               // said rather than printed as the string "null". A redirect that
               // HttpURLConnection follows arrives with no query at all — the
               // executor's three parameters are simply gone — and that is a
               // finding, not a formatting accident.
               String query = exchange.getRequestURI().getRawQuery();
               seen.add("request:" + exchange.getRequestMethod()
                     + " " + exchange.getRequestURI().getPath()
                     + " query=" + (query == null ? "<none>" : query));
            }
            Reply reply;
            try
            {
               reply = responder.reply(seq);
            }
            catch (RuntimeException e)
            {
               exchange.sendResponseHeaders(500, -1);
               exchange.close();
               throw e;
            }
            if (reply.location != null)
            {
               exchange.getResponseHeaders().add("Location", reply.location);
            }
            // -1 means "no body at all"; 0 would mean "chunked, length unknown",
            // and a 204 with a chunked body is not a thing a server may send.
            exchange.sendResponseHeaders(
                  reply.status, reply.body.length == 0 ? -1 : reply.body.length);
            if (reply.body.length > 0)
            {
               OutputStream out = exchange.getResponseBody();
               try
               {
                  out.write(reply.body);
               }
               finally
               {
                  out.close();
               }
            }
            exchange.close();
         }
      });
      this.server.start();
      this.port = this.server.getAddress().getPort();
   }

   public static Http start(Responder responder) throws IOException
   {
      return new Http(false, responder);
   }

   /** As {@link #start}, for a fixture whose request count the clock decides. */
   public static Http startLoggingFirstOnly(Responder responder) throws IOException
   {
      return new Http(true, responder);
   }

   public String base(String contextRoot)
   {
      return "http://127.0.0.1:" + port + contextRoot;
   }

   public synchronized int requests()
   {
      return requests;
   }

   /** The logged requests. Call after {@link #stop}, so the list is settled. */
   public List<String> seen()
   {
      return new ArrayList<String>(seen);
   }

   public void stop()
   {
      server.stop(0);
   }

   /** Always the same answer, whatever the request number. */
   public static Responder always(final Reply reply)
   {
      return new Responder()
      {
         public Reply reply(int seq)
         {
            return reply;
         }
      };
   }

   /** {@code misses} 404s and then {@code then}, so the retry count is pinned. */
   public static Responder missThen(final int misses, final Reply then)
   {
      return new Responder()
      {
         public Reply reply(int seq)
         {
            return seq <= misses ? new Reply(404, null) : then;
         }
      };
   }

   /** A Java-serialized object, the encoding the executor asks for by name. */
   public static byte[] serialized(Object o) throws IOException
   {
      ByteArrayOutputStream bytes = new ByteArrayOutputStream();
      ObjectOutputStream out = new ObjectOutputStream(bytes);
      try
      {
         out.writeObject(o);
      }
      finally
      {
         out.close();
      }
      return bytes.toByteArray();
   }

   /**
    * A loopback port with nothing behind it.
    *
    * Bound and released so the kernel has certainly not given it to anyone else
    * in the meantime, which is the closest a portable test can get to "refused".
    */
   public static int deadPort() throws IOException
   {
      ServerSocket socket = new ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"));
      int p = socket.getLocalPort();
      socket.close();
      return p;
   }
}
