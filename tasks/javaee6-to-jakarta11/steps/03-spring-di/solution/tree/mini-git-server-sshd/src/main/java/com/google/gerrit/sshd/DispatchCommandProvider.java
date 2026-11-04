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

import org.apache.sshd.server.command.Command;
import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * Creates DispatchCommand over the commands registered below one prefix.
 * <p>
 * The command table is built from the names the commands are registered under,
 * aliases included, and holds a factory rather than a command: a command is a
 * prototype, so the table can be built once and still hand out a fresh command
 * on every invocation.
 */
public class DispatchCommandProvider implements ObjectFactory<DispatchCommand> {
  private final ConfigurableListableBeanFactory beanFactory;
  private final String prefix;

  private volatile Map<String, ObjectFactory<Command>> map;

  public DispatchCommandProvider(
      final ConfigurableListableBeanFactory beanFactory, final String prefix) {
    this.beanFactory = beanFactory;
    this.prefix = prefix;
  }

  /**
   * Creates a dispatcher for the container to inject.
   * <p>
   * Every dispatcher but the root one is itself a command, so it is declared as
   * a bean and whatever returns it from a {@code @Bean} method has it injected.
   */
  @Override
  public DispatchCommand getObject() {
    return new DispatchCommand(prefix, getMap());
  }

  /**
   * Creates a dispatcher and injects it by hand.
   * <p>
   * The root dispatcher is the one a client reaches by naming no command at
   * all, so it is the one dispatcher that cannot be a bean: it has no name to
   * register under, and any name given to it would appear in its own table.
   */
  DispatchCommand createInjected() {
    final DispatchCommand cmd = getObject();
    beanFactory.autowireBean(cmd);
    return cmd;
  }

  private Map<String, ObjectFactory<Command>> getMap() {
    if (map == null) {
      synchronized (this) {
        if (map == null) {
          map = createMap();
        }
      }
    }
    return map;
  }

  private Map<String, ObjectFactory<Command>> createMap() {
    final Map<String, ObjectFactory<Command>> m =
        new TreeMap<>();

    for (final String name : allCommands()) {
      if (Commands.isChild(prefix, name)) {
        m.put(Commands.baseNameOf(name),
            () -> beanFactory.getBean(name, Command.class));
      }
    }

    return Collections.unmodifiableMap(new LinkedHashMap<>(m));
  }

  /**
   * Every name a command is registered under in this context, aliases
   * included. A command is a {@link Command} <em>bean</em>: the name it
   * answers to is the name it was declared under, so a class that merely
   * implements {@link Command} without being declared -- the greeting a
   * client that asked for no command gets -- is not one of these.
   */
  private List<String> allCommands() {
    // Deliberately not searching ancestors: a command belongs to the context
    // that declared it, and an ancestor's commands are not ours to dispatch.
    final List<String> names = new ArrayList<>();
    for (final String name : beanFactory.getBeanNamesForType(Command.class)) {
      names.add(name);
      Collections.addAll(names, beanFactory.getAliases(name));
    }
    return names;
  }
}
