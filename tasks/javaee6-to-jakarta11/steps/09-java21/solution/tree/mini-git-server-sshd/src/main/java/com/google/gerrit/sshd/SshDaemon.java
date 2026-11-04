// Copyright (C) 2008 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.google.gerrit.sshd;

import com.google.gerrit.common.Version;
import com.google.gerrit.server.config.ConfigUtil;
import com.google.gerrit.server.config.GerritServerConfig;
import com.google.gerrit.server.ssh.HostKey;
import com.google.gerrit.server.ssh.SshInfo;
import com.google.gerrit.server.util.IdGenerator;
import com.google.gerrit.server.util.SocketUtil;

import org.apache.sshd.common.BaseBuilder;
import org.apache.sshd.common.BuiltinFactory;
import org.apache.sshd.common.NamedFactory;
import org.apache.sshd.common.NamedResource;
import org.apache.sshd.common.channel.ChannelFactory;
import org.apache.sshd.common.cipher.BuiltinCiphers;
import org.apache.sshd.common.compression.BuiltinCompressions;
import org.apache.sshd.common.compression.Compression;
import org.apache.sshd.common.config.keys.KeyUtils;
import org.apache.sshd.common.file.nonefs.NoneFileSystemFactory;
import org.apache.sshd.common.io.IoAcceptor;
import org.apache.sshd.common.kex.KeyExchangeFactory;
import org.apache.sshd.common.keyprovider.KeyPairProvider;
import org.apache.sshd.common.mac.BuiltinMacs;
import org.apache.sshd.common.random.SingletonRandomFactory;
import org.apache.sshd.common.signature.BuiltinSignatures;
import org.apache.sshd.common.signature.Signature;
import org.apache.sshd.common.util.buffer.Buffer;
import org.apache.sshd.common.util.buffer.ByteArrayBuffer;
import org.apache.sshd.common.util.security.SecurityUtils;
import org.apache.sshd.core.CoreModuleProperties;
import org.apache.sshd.server.ServerBuilder;
import org.apache.sshd.server.SshServer;
import org.apache.sshd.server.auth.UserAuthFactory;
import org.apache.sshd.server.auth.pubkey.PublickeyAuthenticator;
import org.apache.sshd.server.auth.pubkey.UserAuthPublicKeyFactory;
import org.apache.sshd.server.channel.ChannelSessionFactory;
import org.apache.sshd.server.command.CommandFactory;
import org.apache.sshd.server.forward.DirectTcpipFactory;
import org.apache.sshd.server.forward.RejectAllForwardingFilter;
import org.apache.sshd.server.session.ServerSessionImpl;
import org.apache.sshd.server.session.SessionFactory;
import org.apache.sshd.server.subsystem.SubsystemFactory;
import org.eclipse.jgit.lib.Config;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.SmartLifecycle;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.security.GeneralSecurityException;
import java.security.KeyPair;
import java.security.PublicKey;
import java.time.Duration;
import java.util.*;

import static java.lang.Thread.sleep;
import static java.util.concurrent.TimeUnit.SECONDS;

/**
 * SSH daemon to communicate with Gerrit.
 * <p>
 * Use a Git URL such as <code>ssh://${email}@${host}:${port}/${path}</code>,
 * e.g. <code>ssh://sop@google.com@gerrit.com:8010/tools/gerrit.git</code> to
 * access the SSH daemon itself.
 * <p>
 * Versions of Git before 1.5.3 may require setting the username and port
 * properties in the user's <code>~/.ssh/config</code> file, and using a host
 * alias through a URL such as <code>gerrit-alias:/tools/gerrit.git:
 * <pre>
 * Host gerrit-alias
 *  User sop@google.com
 *  Hostname gerrit.com
 *  Port 8010
 * </pre>
 */
public class SshDaemon extends SshServer implements SshInfo, SmartLifecycle {
  /** Second up and second from last down: it logs through the appender below. */
  private static final int PHASE = 2;

  private static final int IANA_SSH_PORT = 22;
  public static final int DEFAULT_PORT = 29418;

  private static final Logger log = LoggerFactory.getLogger(SshDaemon.class);

  private final List<SocketAddress> listen;
  private final List<HostKey> hostKeys;

	public static void main(String[] args) {
		// Refreshing the context starts the daemon: it is one of the beans that
		// declare when they come up, so there is nothing to start by hand here.
		new AnnotationConfigApplicationContext(SshConfiguration.class)
				.getBean(SshDaemon.class);
		System.out.println("Started...");
		while (true) {
			try { sleep(1000L); } catch (InterruptedException e) {}
		}
	}

  @Autowired
  SshDaemon(final CommandFactory commandFactory, final NoShell noShell,
      final PublickeyAuthenticator userAuth,
      final KeyPairProvider hostKeyProvider, final IdGenerator idGenerator,
      @GerritServerConfig final Config cfg, final SshLog sshLog) {
    setPort(IANA_SSH_PORT /* never used */);

    listen = parseListen(cfg);

    // Socket options and session limits are properties of the factory manager
    // in SSHD 2.x, not fields on the server or on a MINA session config.
    //
    CoreModuleProperties.SOCKET_REUSEADDR.set(this,
        cfg.getBoolean("sshd", "reuseaddress", true));
    CoreModuleProperties.SOCKET_KEEPALIVE.set(this,
        cfg.getBoolean("sshd", "tcpkeepalive", true));

    CoreModuleProperties.SERVER_IDENTIFICATION.set(this,
        "GerritCodeReview_" + Version.getVersion() //
            + " (" + super.getVersion() + ")");

    CoreModuleProperties.MAX_AUTH_REQUESTS.set(this,
        cfg.getInt("sshd", "maxAuthTries", 6));

    CoreModuleProperties.AUTH_TIMEOUT.set(this,
        Duration.ofSeconds(ConfigUtil.getTimeUnit(cfg, "sshd", null,
            "loginGraceTime", 120, SECONDS)));

    final int maxConnectionsPerUser =
        cfg.getInt("sshd", "maxConnectionsPerUser", 64);
    if (0 < maxConnectionsPerUser) {
      CoreModuleProperties.MAX_CONCURRENT_SESSIONS.set(this,
          maxConnectionsPerUser);
    }

    initKeyExchanges();
    initRandom();
    initCiphers(cfg);
    initMacs(cfg);
    initSignatures();
    initChannels();
    initForwardingFilter();
    initFileSystemFactory();
    initSubsystems();
    initCompression();
    initUserAuth(userAuth);
    setKeyPairProvider(hostKeyProvider);
    setCommandFactory(commandFactory);
    setShellFactory(noShell);
    setSessionFactory(new SessionFactory(this) {
      @Override
      protected ServerSessionImpl setupSession(final ServerSessionImpl s)
          throws Exception {
        final int id = idGenerator.next();
        final SocketAddress peer = s.getIoSession().getRemoteAddress();
        final SshSession sd = new SshSession(id, peer);
        s.setAttribute(SshSession.KEY, sd);

        // Log a session close without authentication as a failure.
        //
        s.addCloseFutureListener(future -> {
          if (sd.isAuthenticationError()) {
            sshLog.onAuthFail(sd);
          }
        });
        return super.setupSession(s);
      }
    });

    hostKeys = computeHostKeys();
  }

  @Override
  public List<HostKey> getHostKeys() {
    return hostKeys;
  }

  public IoAcceptor getIoAcceptor() {
    return acceptor;
  }

  @Override
  public int getPhase() {
    return PHASE;
  }

  /** Running means bound: the acceptor is what {@link #start()} leaves behind. */
  @Override
  public synchronized boolean isRunning() {
    return acceptor != null;
  }

  /**
   * Bind every configured listen address.
   * <p>
   * {@link SshServer#start()} binds the single host/port pair it was
   * configured with; this daemon takes a list, so it drives the same three
   * steps -- validate, build the acceptor, arm the idle timers -- itself. The
   * session factory is wired into the acceptor by {@code createAcceptor()} in
   * SSHD 2.x, so there is no handler to set here.
   */
  @Override
  public synchronized void start() {
    if (acceptor == null) {
      checkConfig();

      acceptor = createAcceptor();
      setupSessionTimeout(getSessionFactory());

      try {
        acceptor.bind(listen);
      } catch (IOException e) {
        throw new IllegalStateException("Cannot bind to " + addressList(), e);
      }

      log.info("Started Gerrit SSHD on " + addressList());
    }
  }

  @Override
  public synchronized void stop() {
    if (acceptor != null) {
      try {
        acceptor.close(true).await();
        log.info("Stopped Gerrit SSHD");
      } catch (IOException e) {
        log.warn("Cannot stop Gerrit SSHD", e);
      } finally {
        acceptor = null;
      }
    }
  }

  @Override
  protected void checkConfig() {
    super.checkConfig();
    if (myHostKeys().isEmpty()) {
      throw new IllegalStateException("No SSHD host key");
    }
  }

  private List<HostKey> computeHostKeys() {
    final List<PublicKey> keys = myHostKeys();
    final ArrayList<HostKey> r = new ArrayList<>();
    for (final PublicKey pub : keys) {
      final Buffer buf = new ByteArrayBuffer();
      buf.putRawPublicKey(pub);
      final byte[] keyBin = buf.getCompactData();
      final String type = KeyUtils.getKeyType(pub);

      for (final InetSocketAddress addr : myAddresses()) {
        r.add(new HostKey(SocketUtil.format(addr, IANA_SSH_PORT), type,
            Base64.getEncoder().encodeToString(keyBin)));
      }
    }
    return Collections.unmodifiableList(r);
  }

  private List<InetSocketAddress> myAddresses() {
    ArrayList<InetSocketAddress> pub = new ArrayList<>();
    ArrayList<InetSocketAddress> local = new ArrayList<>();

    for (final SocketAddress addr : listen) {
      if (addr instanceof final InetSocketAddress inetAddr) {
        if (inetAddr.getAddress().isLoopbackAddress()) {
          local.add(inetAddr);
        } else {
          pub.add(inetAddr);
        }
      }
    }
    return pub.isEmpty() ? local : pub;
  }

  private List<PublicKey> myHostKeys() {
    final KeyPairProvider p = getKeyPairProvider();
    final List<PublicKey> keys = new ArrayList<>(2);
    addPublicKey(keys, p, KeyPairProvider.SSH_RSA);
    addPublicKey(keys, p, KeyPairProvider.SSH_DSS);
    return keys;
  }

  private static void addPublicKey(final Collection<PublicKey> out,
      final KeyPairProvider p, final String type) {
    final KeyPair pair;
    try {
      // No session is in flight while the daemon is being constructed, and
      // SSHD 2.x says a null SessionContext is how you say so.
      pair = p.loadKey(null, type);
    } catch (IOException | GeneralSecurityException e) {
      throw new IllegalStateException("Cannot load " + type + " host key", e);
    }
    if (pair != null && pair.getPublic() != null) {
      out.add(pair.getPublic());
    }
  }

  private String addressList() {
    final StringBuilder r = new StringBuilder();
    for (Iterator<SocketAddress> i = listen.iterator(); i.hasNext();) {
      r.append(SocketUtil.format(i.next(), IANA_SSH_PORT));
      if (i.hasNext()) {
        r.append(", ");
      }
    }
    return r.toString();
  }

  private List<SocketAddress> parseListen(final Config cfg) {
    final ArrayList<SocketAddress> bind = new ArrayList<>(2);
    final String[] want = cfg.getStringList("sshd", null, "listenaddress");
    if (want == null || want.length == 0) {
      bind.add(new InetSocketAddress(DEFAULT_PORT));
      return bind;
    }

    for (final String desc : want) {
      try {
        bind.add(SocketUtil.resolve(desc, DEFAULT_PORT));
      } catch (IllegalArgumentException e) {
        log.error("Bad sshd.listenaddress: " + desc + ": " + e.getMessage());
      }
    }
    return bind;
  }

  /**
   * Key exchange, from the library's own server preference list.
   * <p>
   * The 2010 code chose between two hard-coded Diffie-Hellman groups by
   * asking whether Bouncy Castle happened to be on the classpath. SSHD 2.x
   * publishes a curated preference order and can drop what the JVM cannot
   * do, which is the same question asked properly.
   */
  private void initKeyExchanges() {
    // true = drop the ones this JVM cannot actually complete. The flag reads
    // as "ignore unsupported"; passing false advertises the whole preference
    // list, post-quantum entries included, and the handshake then dies after
    // the client has already picked one of them.
    final List<KeyExchangeFactory> kex =
        ServerBuilder.setUpDefaultKeyExchanges(true);
    if (kex.isEmpty()) {
      throw new IllegalStateException("No supported key exchange");
    }
    setKeyExchangeFactories(kex);
  }

  private void initRandom() {
    setRandomFactory(new SingletonRandomFactory(
        SecurityUtils.getRandomFactory()));
  }

  private void initCiphers(final Config cfg) {
    setCipherFactories(filter(cfg, "cipher",
        supported(ServerBuilder.DEFAULT_SERVER_CIPHERS_PREFERENCE),
        supported(BuiltinCiphers.VALUES)));
  }

  private void initMacs(final Config cfg) {
    setMacFactories(filter(cfg, "mac",
        supported(BaseBuilder.DEFAULT_MAC_PREFERENCE),
        supported(BuiltinMacs.VALUES)));
  }

  /**
   * Drops the entries this JVM cannot run.
   * <p>
   * isSupported() is the library answering the question the old code had to
   * answer for itself, by constructing every cipher with a dummy key and
   * catching InvalidKeyException to find out whether this JVM had the
   * unlimited strength policy files installed.
   */
  private <T> List<NamedFactory<T>> supported(
      final Collection<? extends BuiltinFactory<T>> all) {
    final List<NamedFactory<T>> usable = new ArrayList<>(all.size());
    for (final BuiltinFactory<T> f : all) {
      if (f.isSupported()) {
        usable.add(f);
      } else {
        log.warn("Disabling " + f.getName() + ": not supported by this JVM");
      }
    }
    return usable;
  }

  /**
   * Applies an sshd.&lt;key&gt; setting to a default list.
   * <p>
   * {@code def} is what is offered when the administrator says nothing;
   * {@code avail} is everything a setting may name. The two used to be one
   * array with a null in the middle marking the boundary, which no longer
   * survives contact with a library that walks the list calling getName().
   */
  private static <T> List<NamedFactory<T>> filter(final Config cfg,
      final String key, final List<NamedFactory<T>> def,
      final List<NamedFactory<T>> avail) {
    final String[] want = cfg.getStringList("sshd", null, key);
    if (want == null || want.length == 0) {
      return def;
    }

    boolean didClear = false;
    for (final String setting : want) {
      String name = setting.trim();
      boolean add = true;
      if (name.startsWith("-")) {
        add = false;
        name = name.substring(1).trim();
      } else if (name.startsWith("+")) {
        name = name.substring(1).trim();
      } else if (!didClear) {
        didClear = true;
        def.clear();
      }

      final NamedFactory<T> n = NamedResource.findByName(name,
          String.CASE_INSENSITIVE_ORDER, avail);
      if (n == null) {
        final StringBuilder msg = new StringBuilder();
        msg.append("sshd." + key + " = " + name + " unsupported; only ");
        msg.append(NamedResource.getNames(avail));
        msg.append(" is supported");
        log.error(msg.toString());
      } else if (add) {
        if (!def.contains(n)) {
          def.add(n);
        }
      } else {
        def.remove(n);
      }
    }

    return def;
  }

  private void initSignatures() {
    final List<NamedFactory<Signature>> a = new ArrayList<>();
    for (final BuiltinSignatures f : BaseBuilder.DEFAULT_SIGNATURE_PREFERENCE) {
      if (f.isSupported()) {
        a.add(f);
      }
    }
    setSignatureFactories(a);
  }

  private void initCompression() {
    // Always disable transparent compression. The majority of our data
    // transfer is highly compressed Git pack files. We cannot make them
    // any smaller than they already are.
    //
    setCompressionFactories(Collections
        .<NamedFactory<Compression>> singletonList(BuiltinCompressions.none));
  }

  private void initChannels() {
    setChannelFactories(Arrays.<ChannelFactory> asList(
        ChannelSessionFactory.INSTANCE, //
        DirectTcpipFactory.INSTANCE //
        ));
  }

  private void initSubsystems() {
    setSubsystemFactories(Collections.<SubsystemFactory> emptyList());
  }

  private void initUserAuth(final PublickeyAuthenticator pubkey) {
    setUserAuthFactories(Collections
        .<UserAuthFactory> singletonList(UserAuthPublicKeyFactory.INSTANCE));
    setPublickeyAuthenticator(pubkey);
  }

  /**
   * Refuse agent, X11 and TCP/IP forwarding.
   * <p>
   * Four anonymous {@code return false} overrides in the 0.5 API; SSHD 2.x
   * ships the decision as a singleton.
   */
  private void initForwardingFilter() {
    setForwardingFilter(RejectAllForwardingFilter.INSTANCE);
  }

  private void initFileSystemFactory() {
    setFileSystemFactory(NoneFileSystemFactory.INSTANCE);
  }
}
