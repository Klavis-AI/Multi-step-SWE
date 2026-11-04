package redis;

import javax.crypto.Cipher;
import javax.crypto.EncryptedPrivateKeyInfo;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import javax.net.ssl.*;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.KeyFactory;
import java.security.KeyStore;
import java.security.PrivateKey;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.*;

/**
 * TLS support (src/tls.c) built on {@link SSLEngine} so that the single
 * threaded, non blocking event loop keeps driving the socket: the engine only
 * transforms buffers, every read()/write() still happens in the event loop.
 */
public final class Tls {
    private Tls() {}

    /** Per connection TLS state, attached to {@link Client#tls}. */
    public static final class Conn {
        SSLEngine engine;
        /** raw bytes read from the socket, kept in write mode between calls */
        ByteBuffer netIn;
        /** cipher text that still has to reach the socket, in read mode */
        ByteBuffer netOut = ByteBuffer.allocate(0);
        /** plaintext bytes of c.out already wrapped into netOut but not yet consumed */
        int pendingPlain = 0;
        boolean handshakeDone = false;
    }

    static {
        /* JDK 17 ships "TLS_RSA_*" in jdk.tls.disabledAlgorithms, but OpenSSL's
         * DEFAULT cipher list (what redis uses) still offers them, and the test
         * suite selects them by their OpenSSL names (AES128-SHA256, ...). */
        try {
            String cur = java.security.Security.getProperty("jdk.tls.disabledAlgorithms");
            if (cur != null && cur.contains("TLS_RSA_*")) {
                StringBuilder sb = new StringBuilder();
                for (String tok : cur.split(",")) {
                    String t = tok.trim();
                    if (t.equals("TLS_RSA_*")) continue;
                    if (sb.length() > 0) sb.append(", ");
                    sb.append(t);
                }
                java.security.Security.setProperty("jdk.tls.disabledAlgorithms", sb.toString());
            }
        } catch (Throwable ignored) {
            // keep the JDK defaults
        }
    }

    public static SSLContext context;
    /** the error of the last failed context build, for CONFIG SET */
    public static String lastError = "";

    // ------------------------------------------------------------------
    // PEM loading
    // ------------------------------------------------------------------
    static List<byte[]> pemBlocks(String text, String kind) {
        List<byte[]> out = new ArrayList<>();
        String begin = "-----BEGIN " + kind + "-----";
        String end = "-----END " + kind + "-----";
        int i = 0;
        while (true) {
            int b = text.indexOf(begin, i);
            if (b < 0) break;
            int e = text.indexOf(end, b);
            if (e < 0) break;
            String body = text.substring(b + begin.length(), e).replaceAll("\\s", "");
            out.add(Base64.getDecoder().decode(body));
            i = e + end.length();
        }
        return out;
    }

    static X509Certificate[] loadCerts(String file) throws Exception {
        byte[] raw = Files.readAllBytes(Server.I.resolve(file));
        CertificateFactory cf = CertificateFactory.getInstance("X.509");
        List<X509Certificate> certs = new ArrayList<>();
        for (byte[] der : pemBlocks(new String(raw, StandardCharsets.ISO_8859_1), "CERTIFICATE"))
            certs.add((X509Certificate) cf.generateCertificate(new ByteArrayInputStream(der)));
        if (certs.isEmpty()) throw new IOException("no certificate found in " + file);
        return certs.toArray(new X509Certificate[0]);
    }

    /* ---- minimal DER helpers, to turn a PKCS#1 RSA key into PKCS#8 ---- */
    private static byte[] derLen(int len) {
        if (len < 0x80) return new byte[]{(byte) len};
        if (len < 0x100) return new byte[]{(byte) 0x81, (byte) len};
        if (len < 0x10000) return new byte[]{(byte) 0x82, (byte) (len >> 8), (byte) len};
        return new byte[]{(byte) 0x83, (byte) (len >> 16), (byte) (len >> 8), (byte) len};
    }

    private static byte[] derTlv(int tag, byte[] body) {
        byte[] len = derLen(body.length);
        byte[] out = new byte[1 + len.length + body.length];
        out[0] = (byte) tag;
        System.arraycopy(len, 0, out, 1, len.length);
        System.arraycopy(body, 0, out, 1 + len.length, body.length);
        return out;
    }

    /** rsaEncryption AlgorithmIdentifier: SEQUENCE { OID 1.2.840.113549.1.1.1, NULL } */
    private static final byte[] RSA_ALG_ID = {
        0x30, 0x0d, 0x06, 0x09, 0x2a, (byte) 0x86, 0x48, (byte) 0x86, (byte) 0xf7,
        0x0d, 0x01, 0x01, 0x01, 0x05, 0x00
    };

    static byte[] pkcs1ToPkcs8(byte[] pkcs1) {
        byte[] version = {0x02, 0x01, 0x00};
        byte[] octets = derTlv(0x04, pkcs1);
        byte[] body = new byte[version.length + RSA_ALG_ID.length + octets.length];
        int p = 0;
        System.arraycopy(version, 0, body, p, version.length); p += version.length;
        System.arraycopy(RSA_ALG_ID, 0, body, p, RSA_ALG_ID.length); p += RSA_ALG_ID.length;
        System.arraycopy(octets, 0, body, p, octets.length);
        return derTlv(0x30, body);
    }

    static PrivateKey loadKey(String file, String pass) throws Exception {
        byte[] raw = Files.readAllBytes(Server.I.resolve(file));
        String text = new String(raw, StandardCharsets.ISO_8859_1);
        List<byte[]> enc = pemBlocks(text, "ENCRYPTED PRIVATE KEY");
        if (!enc.isEmpty()) {
            if (pass == null || pass.isEmpty()) throw new IOException("key is encrypted, no password");
            EncryptedPrivateKeyInfo epki = new EncryptedPrivateKeyInfo(enc.get(0));
            /* getAlgName() reports "PBES2" for a PKCS#5 v2 scheme, which is not a
             * SecretKeyFactory name, so try the concrete PBKDF2 variants too. */
            String[] algs = {epki.getAlgName(), "PBEWithHmacSHA256AndAES_256",
                             "PBEWithHmacSHA1AndAES_256", "PBEWithHmacSHA512AndAES_256",
                             "PBEWithHmacSHA256AndAES_128", "PBEWithHmacSHA1AndAES_128"};
            Exception last = null;
            for (String alg : algs) {
                try {
                    SecretKeyFactory skf = SecretKeyFactory.getInstance(alg);
                    Cipher cipher = Cipher.getInstance(alg);
                    cipher.init(Cipher.DECRYPT_MODE,
                                skf.generateSecret(new PBEKeySpec(pass.toCharArray())),
                                epki.getAlgParameters());
                    PKCS8EncodedKeySpec spec = epki.getKeySpec(cipher);
                    return KeyFactory.getInstance("RSA").generatePrivate(spec);
                } catch (Exception e) {
                    last = e;
                }
            }
            throw new IOException("cannot decrypt " + file + ": " + last);
        }
        List<byte[]> p8 = pemBlocks(text, "PRIVATE KEY");
        if (!p8.isEmpty()) {
            PKCS8EncodedKeySpec spec = new PKCS8EncodedKeySpec(p8.get(0));
            for (String alg : new String[]{"RSA", "EC", "DSA"}) {
                try { return KeyFactory.getInstance(alg).generatePrivate(spec); }
                catch (Exception ignored) { /* try the next algorithm */ }
            }
            throw new IOException("unsupported private key in " + file);
        }
        List<byte[]> rsa = pemBlocks(text, "RSA PRIVATE KEY");
        if (!rsa.isEmpty()) {
            if (text.contains("Proc-Type: 4,ENCRYPTED"))
                throw new IOException("legacy encrypted PEM keys are not supported");
            PKCS8EncodedKeySpec spec = new PKCS8EncodedKeySpec(pkcs1ToPkcs8(rsa.get(0)));
            return KeyFactory.getInstance("RSA").generatePrivate(spec);
        }
        throw new IOException("no private key found in " + file);
    }

    // ------------------------------------------------------------------
    // context
    // ------------------------------------------------------------------
    public static boolean configured(Server s) {
        String cert = s.config.getOr("tls-cert-file", "");
        return cert != null && !cert.isEmpty();
    }

    /** Rebuild the SSL context from the current tls-* parameters. */
    public static boolean buildContext(Server s) {
        try {
            X509Certificate[] chain = loadCerts(s.config.getOr("tls-cert-file", ""));
            PrivateKey key = loadKey(s.config.getOr("tls-key-file", ""),
                                     s.config.getOr("tls-key-file-pass", ""));
            KeyStore ks = KeyStore.getInstance("PKCS12");
            ks.load(null, null);
            ks.setKeyEntry("server", key, new char[0], chain);
            KeyManagerFactory kmf = KeyManagerFactory.getInstance("SunX509");
            kmf.init(ks, new char[0]);

            TrustManager[] tms;
            String ca = s.config.getOr("tls-ca-cert-file", "");
            if (ca != null && !ca.isEmpty()) {
                KeyStore ts = KeyStore.getInstance("PKCS12");
                ts.load(null, null);
                int i = 0;
                for (X509Certificate c : loadCerts(ca)) ts.setCertificateEntry("ca" + (i++), c);
                TrustManagerFactory tmf = TrustManagerFactory.getInstance("PKIX");
                tmf.init(ts);
                tms = tmf.getTrustManagers();
            } else {
                tms = null;
            }
            SSLContext ctx = SSLContext.getInstance("TLS");
            ctx.init(kmf.getKeyManagers(), tms, null);
            context = ctx;
            lastError = "";
            return true;
        } catch (Exception e) {
            lastError = String.valueOf(e.getMessage());
            return false;
        }
    }

    /* OpenSSL cipher names -> JSSE suite names, for the suites the test suite uses. */
    private static final String[][] CIPHER_MAP = {
        {"AES128-SHA", "TLS_RSA_WITH_AES_128_CBC_SHA"},
        {"AES256-SHA", "TLS_RSA_WITH_AES_256_CBC_SHA"},
        {"AES128-SHA256", "TLS_RSA_WITH_AES_128_CBC_SHA256"},
        {"AES256-SHA256", "TLS_RSA_WITH_AES_256_CBC_SHA256"},
        {"AES128-GCM-SHA256", "TLS_RSA_WITH_AES_128_GCM_SHA256"},
        {"AES256-GCM-SHA384", "TLS_RSA_WITH_AES_256_GCM_SHA384"},
        {"ECDHE-RSA-AES128-SHA", "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA"},
        {"ECDHE-RSA-AES256-SHA", "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA"},
        {"ECDHE-RSA-AES128-SHA256", "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256"},
        {"ECDHE-RSA-AES256-SHA384", "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384"},
        {"ECDHE-RSA-AES128-GCM-SHA256", "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256"},
        {"ECDHE-RSA-AES256-GCM-SHA384", "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384"},
        {"DHE-RSA-AES128-GCM-SHA256", "TLS_DHE_RSA_WITH_AES_128_GCM_SHA256"},
        {"DHE-RSA-AES256-GCM-SHA384", "TLS_DHE_RSA_WITH_AES_256_GCM_SHA384"},
        {"DHE-RSA-AES128-SHA256", "TLS_DHE_RSA_WITH_AES_128_CBC_SHA256"},
        {"DHE-RSA-AES256-SHA256", "TLS_DHE_RSA_WITH_AES_256_CBC_SHA256"},
    };

    static String javaCipher(String openssl) {
        for (String[] m : CIPHER_MAP) if (m[0].equalsIgnoreCase(openssl)) return m[1];
        return null;
    }

    /** OpenSSL's DEFAULT list: everything but the anonymous/NULL/legacy suites. */
    static List<String> defaultSuites(SSLEngine e) {
        List<String> out = new ArrayList<>();
        for (String s : e.getSupportedCipherSuites()) {
            if (s.contains("_anon_") || s.contains("_NULL_") || s.contains("_EXPORT")
                    || s.contains("_DES_") || s.contains("_RC4_") || s.endsWith("_MD5")
                    || s.startsWith("TLS_EMPTY")) continue;
            out.add(s);
        }
        return out;
    }

    /** Apply "tls-ciphers" (an OpenSSL cipher list) to the supported default set. */
    static String[] cipherList(SSLEngine e, String spec) {
        List<String> supported = defaultSuites(e);
        if (spec == null || spec.trim().isEmpty()) return supported.toArray(new String[0]);
        List<String> all = Arrays.asList(e.getSupportedCipherSuites());
        List<String> out = new ArrayList<>();
        for (String tok : spec.split("[:, ]+")) {
            if (tok.isEmpty()) continue;
            boolean remove = tok.startsWith("-") || tok.startsWith("!");
            if (remove) tok = tok.substring(1);
            if (tok.equalsIgnoreCase("DEFAULT") || tok.equalsIgnoreCase("ALL")
                    || tok.equalsIgnoreCase("HIGH") || tok.equalsIgnoreCase("COMPLEMENTOFDEFAULT")) {
                if (remove) out.clear();
                else for (String s : supported) if (!out.contains(s)) out.add(s);
                continue;
            }
            String j = javaCipher(tok);
            if (j == null) continue;
            if (remove) out.remove(j);
            else if (all.contains(j) && !out.contains(j)) out.add(j);
        }
        if (out.isEmpty()) return supported.toArray(new String[0]);
        /* TLS 1.3 suites are not selectable through the OpenSSL 1.2 names, keep them. */
        for (String s : supported) if (s.startsWith("TLS_AES_") || s.startsWith("TLS_CHACHA20")) {
            if (!out.contains(s)) out.add(s);
        }
        return out.toArray(new String[0]);
    }

    static String[] protocolList(SSLEngine e, String spec) {
        if (spec == null || spec.trim().isEmpty()) {
            List<String> d = new ArrayList<>();
            for (String p : e.getSupportedProtocols())
                if (p.equals("TLSv1.2") || p.equals("TLSv1.3")) d.add(p);
            return d.toArray(new String[0]);
        }
        List<String> out = new ArrayList<>();
        for (String tok : spec.split("[\\s,:]+")) {
            if (tok.isEmpty()) continue;
            for (String p : e.getSupportedProtocols()) if (p.equalsIgnoreCase(tok)) out.add(p);
        }
        if (out.isEmpty()) out.add("TLSv1.2");
        return out.toArray(new String[0]);
    }

    // ------------------------------------------------------------------
    // per connection
    // ------------------------------------------------------------------
    public static Conn newConn(Server s) {
        if (context == null && !buildContext(s)) return null;
        Conn t = new Conn();
        SSLEngine e = context.createSSLEngine();
        e.setUseClientMode(false);
        try { e.setEnabledProtocols(protocolList(e, s.config.getOr("tls-protocols", ""))); }
        catch (Exception ignored) { }
        try { e.setEnabledCipherSuites(cipherList(e, s.config.getOr("tls-ciphers", ""))); }
        catch (Exception ignored) { }
        SSLParameters p = e.getSSLParameters();
        p.setUseCipherSuitesOrder(s.config.getBool("tls-prefer-server-ciphers"));
        e.setSSLParameters(p);
        String auth = s.config.getOr("tls-auth-clients", "yes");
        if ("no".equalsIgnoreCase(auth)) { e.setNeedClientAuth(false); e.setWantClientAuth(false); }
        else if ("optional".equalsIgnoreCase(auth)) e.setWantClientAuth(true);
        else e.setNeedClientAuth(true);
        t.engine = e;
        int pkt = e.getSession().getPacketBufferSize();
        t.netIn = ByteBuffer.allocate(Math.max(pkt, 16 * 1024) * 2);
        return t;
    }

    static void grow(Conn t, int extra) {
        ByteBuffer bigger = ByteBuffer.allocate(t.netIn.capacity() + extra);
        t.netIn.flip();
        bigger.put(t.netIn);
        t.netIn = bigger;
    }

    /** Append the cipher text produced by the engine to the pending output. */
    static void queue(Conn t, ByteBuffer src) {
        if (!src.hasRemaining()) return;
        ByteBuffer merged = ByteBuffer.allocate(t.netOut.remaining() + src.remaining());
        merged.put(t.netOut);
        merged.put(src);
        merged.flip();
        t.netOut = merged;
    }

    static void runTasks(Conn t) {
        Runnable task;
        while ((task = t.engine.getDelegatedTask()) != null) task.run();
    }

    /** Drive the handshake as far as it can go without new network input. */
    static void handshake(Server s, Client c) throws IOException {
        Conn t = c.tls;
        SSLEngineResult.HandshakeStatus st = t.engine.getHandshakeStatus();
        while (true) {
            switch (st) {
                case NEED_TASK:
                    runTasks(t);
                    st = t.engine.getHandshakeStatus();
                    break;
                case NEED_WRAP: {
                    ByteBuffer out = ByteBuffer.allocate(t.engine.getSession().getPacketBufferSize());
                    SSLEngineResult r = t.engine.wrap(ByteBuffer.allocate(0), out);
                    out.flip();
                    queue(t, out);
                    st = r.getHandshakeStatus();
                    if (r.getStatus() == SSLEngineResult.Status.CLOSED) return;
                    break;
                }
                default:
                    return;
            }
        }
    }

    public static boolean wantWrite(Client c) {
        return c.tls != null && c.tls.netOut.hasRemaining();
    }

    /** Push whatever cipher text is pending; returns true when all of it went out. */
    static boolean flushNet(Client c) throws IOException {
        Conn t = c.tls;
        if (!t.netOut.hasRemaining()) return true;
        c.chan.write(t.netOut);
        return !t.netOut.hasRemaining();
    }

    /** Server.readFromClient() for a TLS connection. */
    public static void read(Server s, Client c) throws IOException {
        Conn t = c.tls;
        if (!t.netIn.hasRemaining()) grow(t, t.netIn.capacity());
        int n;
        try { n = c.chan.read(t.netIn); }
        catch (IOException e) { s.freeClient(c); return; }
        if (n < 0) { s.freeClient(c); return; }
        s.statTotalNetInput += n;
        c.lastInteraction = System.currentTimeMillis();

        ByteBuffer app = ByteBuffer.allocate(Math.max(16 * 1024,
                t.engine.getSession().getApplicationBufferSize()));
        boolean progress = true;
        while (progress) {
            progress = false;
            handshake(s, c);
            t.netIn.flip();
            SSLEngineResult r;
            try {
                r = t.engine.unwrap(t.netIn, app);
            } catch (SSLException e) {
                t.netIn.compact();
                s.logNotice("Error accepting a client connection: " + e.getMessage());
                s.freeClient(c);
                return;
            }
            t.netIn.compact();
            if (r.bytesProduced() > 0) {
                app.flip();
                byte[] plain = new byte[app.remaining()];
                app.get(plain);
                app.clear();
                c.query.put(plain);
            }
            switch (r.getStatus()) {
                case OK:
                    /* No forward progress means there is nothing left to do. */
                    progress = r.bytesConsumed() > 0 || r.bytesProduced() > 0
                            || t.engine.getHandshakeStatus() == SSLEngineResult.HandshakeStatus.NEED_WRAP
                            || t.engine.getHandshakeStatus() == SSLEngineResult.HandshakeStatus.NEED_TASK;
                    break;
                case BUFFER_UNDERFLOW:
                    break;
                case BUFFER_OVERFLOW:
                    app = ByteBuffer.allocate(app.capacity() * 2);
                    progress = true;
                    break;
                case CLOSED:
                    try { flushNet(c); } catch (IOException ignored) { }
                    s.freeClient(c);
                    return;
                default:
                    break;
            }
            if (t.netIn.position() == 0 && r.getStatus() == SSLEngineResult.Status.BUFFER_UNDERFLOW) break;
        }
        handshake(s, c);
        if (!t.handshakeDone) {
            SSLEngineResult.HandshakeStatus hs = t.engine.getHandshakeStatus();
            if ((hs == SSLEngineResult.HandshakeStatus.NOT_HANDSHAKING
                    || hs == SSLEngineResult.HandshakeStatus.FINISHED)
                    && !t.engine.getSession().getCipherSuite().contains("NULL")) {
                t.handshakeDone = true;
            }
        }
        try { flushNet(c); } catch (IOException e) { s.freeClient(c); return; }
        if (c.query.len > 0) s.processInput(c);
    }

    /** config.c applyTLSPort(): (re)bind the plain and the TLS listeners. */
    public static void updateListeners(Server s) throws IOException {
        int newPort = s.config.getInt("port");
        int newTls = s.config.getInt("tls-port");
        if (newPort != s.port) {
            if (s.tcpServer != null) { s.tcpServer.close(); s.tcpServer = null; }
            s.port = newPort;
            if (newPort != 0) s.tcpServer = listen(s, newPort);
        }
        if (newTls != s.tlsPort) {
            if (s.tlsServer != null) { s.tlsServer.close(); s.tlsServer = null; }
            s.tlsPort = newTls;
            if (newTls != 0) s.tlsServer = listen(s, newTls);
        }
    }

    static java.nio.channels.ServerSocketChannel listen(Server s, int port) throws IOException {
        java.nio.channels.ServerSocketChannel ss = java.nio.channels.ServerSocketChannel.open();
        ss.configureBlocking(false);
        ss.socket().setReuseAddress(true);
        String b = s.config.getOr("bind", "");
        String addr = null;
        if (b != null && !b.trim().isEmpty()) {
            for (String tok : b.trim().split("\\s+")) {
                if (tok.startsWith("-")) continue;
                if (tok.equals("*")) { addr = null; break; }
                addr = tok;
                break;
            }
        }
        ss.bind(addr == null ? new java.net.InetSocketAddress(port)
                             : new java.net.InetSocketAddress(addr, port), 511);
        ss.register(s.selector, java.nio.channels.SelectionKey.OP_ACCEPT);
        return ss;
    }

    /** Client.write() for a TLS connection. */
    public static void write(Client c) throws IOException {
        Conn t = c.tls;
        if (c.chan == null) return;
        if (t.netOut.hasRemaining()) {
            if (!flushNet(c)) return;
            if (t.pendingPlain > 0) { c.out.consume(t.pendingPlain); t.pendingPlain = 0; }
        }
        if (!t.handshakeDone || c.out.len == 0) return;
        ByteBuffer src = ByteBuffer.wrap(c.out.a, 0, c.out.len);
        int pkt = t.engine.getSession().getPacketBufferSize();
        ByteBuffer out = ByteBuffer.allocate(Math.max(c.out.len + 2048, pkt));
        int consumed = 0;
        while (src.hasRemaining()) {
            while (out.remaining() < pkt) {
                ByteBuffer bigger = ByteBuffer.allocate(out.capacity() * 2);
                out.flip();
                bigger.put(out);
                out = bigger;
            }
            SSLEngineResult r = t.engine.wrap(src, out);
            consumed += r.bytesConsumed();
            if (r.getStatus() != SSLEngineResult.Status.OK) break;
            if (r.bytesConsumed() == 0 && r.bytesProduced() == 0) break;
        }
        out.flip();
        queue(t, out);
        t.pendingPlain = consumed;
        if (flushNet(c) && t.pendingPlain > 0) {
            c.out.consume(t.pendingPlain);
            t.pendingPlain = 0;
        }
    }
}
