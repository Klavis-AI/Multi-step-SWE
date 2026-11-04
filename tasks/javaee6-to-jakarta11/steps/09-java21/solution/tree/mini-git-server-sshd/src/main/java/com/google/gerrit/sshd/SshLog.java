// Copyright (C) 2009 The Android Open Source Project
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

import com.google.gerrit.server.CurrentUser;
import com.google.gerrit.server.IdentifiedUser;
import com.google.gerrit.server.config.SitePaths;
import com.google.gerrit.server.util.IdGenerator;
import com.google.gerrit.sshd.SshScope.Context;

import ch.qos.logback.classic.AsyncAppender;
import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.filter.ThresholdFilter;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.classic.spi.LoggingEvent;
import ch.qos.logback.core.LayoutBase;
import ch.qos.logback.core.encoder.LayoutWrappingEncoder;
import ch.qos.logback.core.rolling.RollingFileAppender;
import ch.qos.logback.core.rolling.TimeBasedRollingPolicy;
import ch.qos.logback.core.status.OnErrorConsoleStatusListener;

import org.eclipse.jgit.util.QuotedString;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.SmartLifecycle;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.HashMap;
import java.util.Map;

class SshLog implements SmartLifecycle {
  private static final String LOG_NAME = "sshd_log";
  private static final String P_SESSION = "session";
  private static final String P_USER_NAME = "userName";
  private static final String P_ACCOUNT_ID = "accountId";
  private static final String P_WAIT = "queueWaitTime";
  private static final String P_EXEC = "executionTime";
  private static final String P_STATUS = "status";

  /** First up and last down: the daemon above it logs through this. */
  private static final int PHASE = 1;

  private final ObjectProvider<SshSession> session;
  private final ObjectProvider<Context> context;
  private final LoggerContext loggerContext;
  private final AsyncAppender async;
  private volatile boolean running;

  @Autowired
  SshLog(final ObjectProvider<SshSession> session,
      final ObjectProvider<Context> context,
      final SitePaths site) {
    this.session = session;
    this.context = context;
    this.loggerContext = (LoggerContext) LoggerFactory.getILoggerFactory();

    // Logback reports an appender's own troubles through the context's status
    // manager rather than through a per-appender error handler, so the
    // listener below is what the old LogLogHandler was: errors raised after
    // the appender is up reach an operator instead of being swallowed.
    final OnErrorConsoleStatusListener onError = new OnErrorConsoleStatusListener();
    onError.setContext(loggerContext);
    onError.start();
    loggerContext.getStatusManager().add(onError);

    final String path = new File(resolve(site.logs_dir), LOG_NAME).getPath();

    final MyLayout layout = new MyLayout();
    layout.setContext(loggerContext);
    layout.start();

    final LayoutWrappingEncoder<ILoggingEvent> encoder = new LayoutWrappingEncoder<>();
    encoder.setContext(loggerContext);
    encoder.setCharset(StandardCharsets.UTF_8);
    encoder.setLayout(layout);
    encoder.start();

    final RollingFileAppender<ILoggingEvent> dst = new RollingFileAppender<>();
    dst.setContext(loggerContext);
    dst.setName(LOG_NAME);
    dst.setFile(path);
    dst.setAppend(true);
    dst.setImmediateFlush(true);
    dst.setEncoder(encoder);

    // `DailyRollingFileAppender` in one class is a rolling appender plus a
    // time-based policy in two, and the daily pattern is the same one the
    // old appender hard-coded.
    final TimeBasedRollingPolicy<ILoggingEvent> policy = new TimeBasedRollingPolicy<>();
    policy.setContext(loggerContext);
    policy.setParent(dst);
    policy.setFileNamePattern(path + ".%d{yyyy-MM-dd}");
    policy.start();
    dst.setRollingPolicy(policy);

    final ThresholdFilter threshold = new ThresholdFilter();
    threshold.setContext(loggerContext);
    threshold.setLevel(Level.INFO.toString());
    threshold.start();
    dst.addFilter(threshold);

    dst.start();
    if (!dst.isStarted()) {
      // What DieErrorHandler did: a site whose log directory cannot be
      // written to is a misconfiguration, and starting anyway would mean
      // running an audited service with no audit.
      throw new RuntimeException("Cannot open log file: " + path);
    }

    async = new AsyncAppender();
    async.setContext(loggerContext);
    async.setName(LOG_NAME);
    async.setQueueSize(64);
    // Logback's async appender drops events at or below INFO once the queue
    // is four-fifths full, which every record written here is. Zero turns
    // that off: this is an audit log, and a dropped record is a lost one.
    async.setDiscardingThreshold(0);
    async.setNeverBlock(false);
    async.setIncludeCallerData(false);
    async.setMaxFlushTime(5000);
    async.addAppender(dst);
    async.start();
  }

  @Override
  public int getPhase() {
    return PHASE;
  }

  @Override
  public boolean isRunning() {
    return running;
  }

  @Override
  public void start() {
    running = true;
  }

  @Override
  public void stop() {
    running = false;
    async.stop();
  }

  void onLogin() {
    append("LOGIN FROM " + session.getObject().getRemoteAddressAsString(),
        properties());
  }

  void onAuthFail(final SshSession sd) {
    final Map<String, String> props = new HashMap<>();
    put(props, P_SESSION, id(sd.getSessionId()));
    put(props, P_USER_NAME, sd.getUsername());

    final String error = sd.getAuthenticationError();
    if (error != null) {
      props.put(P_STATUS, error);
    }

    append("AUTH FAILURE FROM " + sd.getRemoteAddressAsString(), props);
  }

  void onExecute(int exitValue) {
    final Context ctx = context.getObject();
    ctx.finished = System.currentTimeMillis();

    final String commandLine = ctx.getCommandLine();
    String cmd = QuotedString.BOURNE.quote(commandLine);
    if (cmd == commandLine) {
      cmd = "'" + commandLine + "'";
    }

    final Map<String, String> props = properties();
    props.put(P_WAIT, (ctx.started - ctx.created) + "ms");
    props.put(P_EXEC, (ctx.finished - ctx.started) + "ms");

    final String status;
    switch (exitValue) {
      case BaseCommand.STATUS_CANCEL:
        status = "killed";
        break;

      case BaseCommand.STATUS_NOT_FOUND:
        status = "not-found";
        break;

      case BaseCommand.STATUS_NOT_ADMIN:
        status = "not-admin";
        break;

      default:
        status = String.valueOf(exitValue);
        break;
    }
    props.put(P_STATUS, status);

    append(cmd, props);
  }

  void onLogout() {
    append("LOGOUT", properties());
  }

  /**
   * The per-record fields, which logback carries as the event's MDC map.
   *
   * <p>Built before the event rather than set on it: logback's LoggingEvent
   * takes its MDC map once and refuses a second, where log4j's took one
   * property at a time.
   */
  private Map<String, String> properties() {
    final SshSession sd = session.getObject();
    final CurrentUser user = sd.getCurrentUser();

    final Map<String, String> props = new HashMap<>();
    put(props, P_SESSION, id(sd.getSessionId()));

    String userName = "-", accountId = "-";

    if (user instanceof IdentifiedUser u) {
      userName = u.getUserName();

    }

    put(props, P_USER_NAME, userName);
    put(props, P_ACCOUNT_ID, accountId);

    return props;
  }

  /** Null values are absent keys, which is what log4j's setProperty did. */
  private static void put(Map<String, String> props, String key, String val) {
    if (val != null) {
      props.put(key, val);
    }
  }

  private void append(final String msg, final Map<String, String> props) {
    final LoggingEvent event = new LoggingEvent();
    event.setLoggerName(LOG_NAME);
    event.setLoggerContextRemoteView(loggerContext.getLoggerContextRemoteView());
    event.setLevel(Level.INFO);
    event.setMessage(msg);
    event.setThreadName("SSHD");
    event.setTimeStamp(System.currentTimeMillis());
    event.setMDCPropertyMap(props);
    async.doAppend(event);
  }

  private static String id(final int id) {
    return IdGenerator.format(id);
  }

  private static File resolve(final File logs_dir) {
    try {
      return logs_dir.getCanonicalFile();
    } catch (IOException e) {
      return logs_dir.getAbsoluteFile();
    }
  }

  private static final class MyLayout extends LayoutBase<ILoggingEvent> {
    // The wire format is unchanged -- `yyyy-MM-dd HH:mm:ss,SSS` followed by an
    // RFC 822 offset -- but a DateTimeFormatter is immutable, so the hand-rolled
    // second-resolution cache and the lock that guarded the shared Calendar are
    // both gone. Nothing here is mutable state any more.
    private static final DateTimeFormatter TIMESTAMP =
        DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss,SSS");
    private static final DateTimeFormatter OFFSET =
        DateTimeFormatter.ofPattern("Z");

    private final ZoneId zone;
    private final String timeZone;

    MyLayout() {
      zone = ZoneId.systemDefault();
      timeZone = OFFSET.format(Instant.now().atZone(zone));
    }

    @Override
    public String doLayout(ILoggingEvent event) {
      final StringBuilder buf = new StringBuilder(128);

      buf.append('[');
      TIMESTAMP.formatTo(
          Instant.ofEpochMilli(event.getTimeStamp()).atZone(zone), buf);
      buf.append(' ');
      buf.append(timeZone);
      buf.append(']');

      req(P_SESSION, buf, event);
      req(P_USER_NAME, buf, event);
      req(P_ACCOUNT_ID, buf, event);

      buf.append(' ');
      buf.append(event.getMessage());

      opt(P_WAIT, buf, event);
      opt(P_EXEC, buf, event);
      opt(P_STATUS, buf, event);

      buf.append('\n');
      return buf.toString();
    }

    private void req(String key, StringBuilder buf, ILoggingEvent event) {
      Object val = event.getMDCPropertyMap().get(key);
      buf.append(' ');
      if (val != null) {
        String s = val.toString();
        if (0 <= s.indexOf(' ')) {
          buf.append(QuotedString.BOURNE.quote(s));
        } else {
          buf.append(val);
        }
      } else {
        buf.append('-');
      }
    }

    private void opt(String key, StringBuilder buf, ILoggingEvent event) {
      Object val = event.getMDCPropertyMap().get(key);
      if (val != null) {
        buf.append(' ');
        buf.append(val);
      }
    }
  }
}
