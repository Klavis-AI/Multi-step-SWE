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


package com.google.gerrit.sshd.commands;

import com.google.gerrit.sshd.DispatchCommandProvider;

import org.apache.sshd.server.command.Command;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.config.ConfigurableBeanFactory;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Scope;

/**
 * Register the basic commands any Gerrit server should support.
 * <p>
 * A command is registered under the name a client types to invoke it, so a
 * subcommand is named for its parent, a space, and itself, and the extra names
 * of a command are aliases of its first one. Every command is a prototype:
 * each invocation is served by a command of its own.
 */
@Configuration(proxyBeanMethods = false)
public class DefaultCommandConfiguration {
  // The following commands can be ran on a server in either Master or Slave
  // mode. If a command should only be used on a server in one mode, but not
  // both, it should be bound in both MasterCommandModule and
  // SlaveCommandModule.

  @Bean
  DispatchCommandProvider gerritDispatcher(
      final ConfigurableListableBeanFactory beanFactory) {
    return new DispatchCommandProvider(beanFactory, "gerrit");
  }

  @Bean
  DispatchCommandProvider gitDispatcher(
      final ConfigurableListableBeanFactory beanFactory) {
    return new DispatchCommandProvider(beanFactory, "git");
  }

  @Bean(name = {"gerrit receive-pack", "git receive-pack", "git-receive-pack"})
  @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
  Command receivePack() {
    return new Receive();
  }

  @Bean(name = "gerrit")
  @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
  Command gerrit(@Qualifier("gerritDispatcher") final DispatchCommandProvider dispatcher) {
    return dispatcher.getObject();
  }

  @Bean(name = "gerrit show-connections")
  @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
  Command showConnections() {
    return new AdminShowConnections();
  }

  @Bean(name = "git")
  @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
  Command git(@Qualifier("gitDispatcher") final DispatchCommandProvider dispatcher) {
    return dispatcher.getObject();
  }

  @Bean(name = {"git upload-pack", "git-upload-pack"})
  @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
  Command uploadPack() {
    return new Upload();
  }

  @Bean(name = "kill")
  @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
  Command kill() {
    return new AdminKill();
  }
}
