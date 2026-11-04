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
import com.google.gerrit.sshd.args4j.SubcommandHandler;

import org.apache.sshd.server.Environment;
import org.apache.sshd.server.channel.ChannelSession;
import org.apache.sshd.server.command.Command;
import org.kohsuke.args4j.Argument;
import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.beans.factory.annotation.Autowired;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Command that dispatches to a subcommand from its command table.
 */
final class DispatchCommand extends BaseCommand {
  @Autowired
  private ObjectProvider<CurrentUser> currentUser;

  private final String prefix;
  private final Map<String, ObjectFactory<Command>> commands;
  private Command cmd;
  private ChannelSession channel;

  @Argument(index = 0, required = true, metaVar = "COMMAND", handler = SubcommandHandler.class)
  private String commandName;

  @Argument(index = 1, multiValued = true, metaVar = "ARG")
  private List<String> args = new ArrayList<>();

  DispatchCommand(final String pfx,
      final Map<String, ObjectFactory<Command>> all) {
    prefix = pfx;
    commands = all;
  }

  @Override
  public void start(final ChannelSession channel, final Environment env)
      throws IOException {
    try {
      parseCommandLine();

      final ObjectFactory<Command> p = commands.get(commandName);
      if (p == null) {
        String msg =
            (prefix.isEmpty() ? "Gerrit Code Review" : prefix) + ": "
                + commandName + ": not found";
        throw new UnloggedFailure(1, msg);
      }

      final Command cmd = p.getObject();

      if (isAdminCommand(cmd) && !currentUser.getObject().isAdministrator()) {
        final String msg = "fatal: Not a Gerrit administrator";
        throw new UnloggedFailure(BaseCommand.STATUS_NOT_ADMIN, msg);
      }

      if (cmd instanceof BaseCommand) {
        final BaseCommand bc = (BaseCommand) cmd;
        if (prefix.isEmpty())
          bc.setName(commandName);
        else
          bc.setName(prefix + " " + commandName);
        bc.setArguments(args.toArray(new String[args.size()]));

      } else if (!args.isEmpty()) {
        throw new UnloggedFailure(1, commandName + " does not take arguments");
      }

      provideStateTo(cmd);

      synchronized (this) {
        this.cmd = cmd;
        this.channel = channel;
      }
      cmd.start(channel, env);

    } catch (UnloggedFailure e) {
      String msg = e.getMessage();
      if (!msg.endsWith("\n")) {
        msg += "\n";
      }
      err.write(msg.getBytes(ENC));
      err.flush();
      onExit(e.exitCode);
    }
  }

  private boolean isAdminCommand(final Command cmd) {
    return cmd.getClass().getAnnotation(AdminCommand.class) != null;
  }

  @Override
  public void destroy(final ChannelSession channel) throws Exception {
    synchronized (this) {
      if (cmd != null) {
        // The subcommand is torn down with the channel it was started on,
        // which is the one recorded here rather than whatever the caller
        // happens to hand us.
        cmd.destroy(this.channel);
        cmd = null;
        this.channel = null;
      }
    }
  }

  @Override
  protected String usage() {
    final StringBuilder usage = new StringBuilder();
    usage.append("Available commands");
    if (!prefix.isEmpty()) {
      usage.append(" of ");
      usage.append(prefix);
    }
    usage.append(" are:\n");
    usage.append("\n");
    for (Map.Entry<String, ObjectFactory<Command>> e : commands.entrySet()) {
      usage.append("   ");
      usage.append(e.getKey());
      usage.append("\n");
    }
    usage.append("\n");

    usage.append("See '");
    if (prefix.indexOf(' ') < 0) {
      usage.append(prefix);
      usage.append(' ');
    }
    usage.append("COMMAND --help' for more information.\n");
    usage.append("\n");
    return usage.toString();
  }
}
