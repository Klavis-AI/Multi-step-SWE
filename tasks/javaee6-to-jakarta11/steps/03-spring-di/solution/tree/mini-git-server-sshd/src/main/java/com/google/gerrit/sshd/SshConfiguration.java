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
import com.google.gerrit.server.RemotePeer;
import com.google.gerrit.server.RequestCleanup;
import com.google.gerrit.server.git.WorkQueue;
import com.google.gerrit.sshd.args4j.SocketAddressHandler;
import com.google.gerrit.sshd.commands.DefaultCommandConfiguration;
import com.google.gerrit.util.cli.CmdLineParser;
import com.google.gerrit.util.cli.OptionHandlerFactory;

import org.apache.sshd.common.keyprovider.KeyPairProvider;
import org.apache.sshd.server.command.CommandFactory;
import org.springframework.beans.factory.BeanFactory;
import org.springframework.beans.factory.config.BeanFactoryPostProcessor;
import org.springframework.beans.factory.config.ConfigurableBeanFactory;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Import;
import org.springframework.context.annotation.Primary;
import org.springframework.context.annotation.Scope;

import java.net.SocketAddress;

/** Configures standard dependencies for {@link com.google.gerrit.sshd.SshDaemon}. */
@Configuration(proxyBeanMethods = false)
@Import({DefaultCommandConfiguration.class, SshLog.class, SshDaemon.class,
    NoShell.class, NoShell.MessageFactory.class,
    ToyPubKeyAuth.class, HostKeyProvider.class, CommandFactoryProvider.class,
    CommandExecutorQueueProvider.class, CommandExecutorProvider.class,
    StreamCommandExecutorProvider.class, SshRemotePeerProvider.class,
    SshCurrentUserProvider.class, SshIdentifiedUserProvider.class})
public class SshConfiguration {
  // Nothing to say about Bouncy Castle here any more: the library picks up
  // whichever security providers are on the classpath by itself, and reads
  // the host keys this server ships without one.

  /**
   * Makes {@code request} mean {@link SshScope#REQUEST} in this context.
   * <p>
   * Static because a bean factory post-processor has to run before anything it
   * configures is created, and a static method needs no instance of this class
   * to call.
   */
  @Bean
  static BeanFactoryPostProcessor sshScopeRegistrar() {
    return beanFactory -> beanFactory.registerScope(SshScope.NAME, SshScope.REQUEST);
  }

  /** Cleanup for one executed command, owned by the command's own context. */
  @Bean
  @Scope(SshScope.NAME)
  RequestCleanup requestCleanup() {
    return SshScope.getContext().getCleanup();
  }

  @Bean
  @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
  SshScope.Context sshScopeContext() {
    return SshScope.getContext();
  }

  @Bean
  @Scope(SshScope.NAME)
  SshSession sshSession() {
    return SshScope.getContext().getSession();
  }

  @Bean
  @RemotePeer
  @Scope(SshScope.NAME)
  SocketAddress remotePeer(final SshRemotePeerProvider provider) {
    return provider.getObject();
  }

  // Primary because the anonymous user of the parent context is a CurrentUser
  // too, and an injection point asking for the interface wants the user of the
  // command being executed.
  @Bean
  @Primary
  @Scope(SshScope.NAME)
  CurrentUser currentUser(final SshCurrentUserProvider provider) {
    return provider.getObject();
  }

  @Bean
  @Scope(SshScope.NAME)
  IdentifiedUser identifiedUser(final SshIdentifiedUserProvider provider) {
    return provider.getObject();
  }

  @Bean
  @CommandExecutor
  @Scope(SshScope.NAME)
  WorkQueue.Executor commandExecutor(final CommandExecutorProvider provider) {
    return provider.getObject();
  }

  @Bean
  @StreamCommandExecutor
  WorkQueue.Executor streamCommandExecutor(
      final StreamCommandExecutorProvider provider) {
    return provider.getObject();
  }

  /** Dispatcher for a bare command name, which is the one no client can type. */
  @Bean
  DispatchCommandProvider rootDispatcher(
      final ConfigurableListableBeanFactory beanFactory) {
    return new DispatchCommandProvider(beanFactory, Commands.ROOT);
  }

  @Bean
  CommandFactory commandFactory(final CommandFactoryProvider provider) {
    return provider.getObject();
  }

  @Bean
  KeyPairProvider hostKeys(final HostKeyProvider provider) {
    return provider.getObject();
  }

  @Bean
  CmdLineParser.Factory cmdLineParserFactory(final BeanFactory beanFactory) {
    return bean -> new CmdLineParser(beanFactory, bean);
  }

  /**
   * Handler for a {@code HOST:PORT} command line argument.
   * <p>
   * The return type is what {@link CmdLineParser} looks the handler up by, so
   * it names the value type this handler parses and cannot be widened.
   */
  @Bean
  OptionHandlerFactory<SocketAddress> socketAddressHandlerFactory() {
    return (parser, option, setter) ->
        new SocketAddressHandler(parser, option, setter);
  }
}
