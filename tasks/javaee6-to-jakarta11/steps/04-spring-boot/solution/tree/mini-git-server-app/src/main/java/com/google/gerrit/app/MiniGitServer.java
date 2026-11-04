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

package com.google.gerrit.app;

import com.google.gerrit.httpd.CanonicalWebUrlConfiguration;
import com.google.gerrit.server.config.GerritGlobalConfiguration;
import com.google.gerrit.server.config.ServerConfigConfiguration;
import com.google.gerrit.server.ssh.SshInfo;
import com.google.gerrit.sshd.SshConfiguration;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ApplicationContext;
import org.springframework.context.ConfigurableApplicationContext;

/**
 * Starts mini-git-server.
 * <p>
 * The server is four application contexts rather than one, and the shape they
 * make is the reason this class exists instead of a bare
 * {@code SpringApplication.run}:
 *
 * <pre>
 *   cfg  (the site directory, and the gerrit.config read out of it)
 *    |
 *   sys  (the repositories, the accounts, the canonical URL)
 *    |
 *    +-- ssh  (the SSH daemon)
 *    +-- web  (the servlet container)
 * </pre>
 *
 * The two leaves are siblings and not one context because each of them
 * declares its own {@code CurrentUser}, {@code IdentifiedUser} and remote
 * peer, scoped to its own kind of request; merged, the container would have
 * two primary candidates for each and refuse to start.
 */
public final class MiniGitServer {
  public static void main(final String[] args) {
    final SpringApplicationBuilder ssh =
        new SpringApplicationBuilder(SitePathConfiguration.class,
            ServerConfigConfiguration.class)
            .web(WebApplicationType.NONE)
            .registerShutdownHook(false)
            .child(GerritGlobalConfiguration.class,
                CanonicalWebUrlConfiguration.class)
            .web(WebApplicationType.NONE)
            .registerShutdownHook(false)
            .child(SshConfiguration.class)
            .web(WebApplicationType.NONE)
            .registerShutdownHook(false);

    // Running the leaf runs its ancestors first, so this one call brings up
    // cfg, then sys, then ssh.
    final ConfigurableApplicationContext sshContext = ssh.run(args);

    // A sibling of the SSH context, so its parent is sys. The one bean the web
    // half borrows from its sibling is handed over here rather than inherited,
    // and it has to be in place before the context refreshes because the
    // servlets that take it are built during the refresh.
    final ConfigurableApplicationContext webContext =
        ssh.sibling(WebApplication.class)
            .web(WebApplicationType.SERVLET)
            .registerShutdownHook(false)
            .initializers(context -> context.getBeanFactory()
                .registerSingleton("sshInfo", sshContext.getBean(SshInfo.class)))
            .run(args);

    // The contexts came up in the order they were created, and they go down in
    // the reverse of it; the beans inside each one that have something to stop
    // do the same among themselves, because closing a context destroys them.
    // Spring Boot's own shutdown hook is declined above precisely so that this
    // order is the one that runs.
    Runtime.getRuntime().addShutdownHook(new Thread(() -> {
      webContext.close();
      sshContext.close();
      closeAncestors(sshContext);
    }, "shutdown"));
  }

  /** Closes the contexts a context inherits from, nearest one first. */
  private static void closeAncestors(final ApplicationContext context) {
    for (ApplicationContext c = context.getParent(); c != null; c = c.getParent()) {
      ((ConfigurableApplicationContext) c).close();
    }
  }

  private MiniGitServer() {
  }
}
