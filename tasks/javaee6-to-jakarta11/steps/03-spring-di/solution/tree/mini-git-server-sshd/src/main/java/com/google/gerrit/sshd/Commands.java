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

/**
 * Utilities for the names SSH commands are registered under.
 * <p>
 * A command is registered under the name a client types to invoke it, so the
 * subcommands of {@code gerrit} are the ones named {@code "gerrit <something>"}
 * and the separator between a command and its parent is a single space.
 */
public class Commands {
  /** Magic value signaling the top level. */
  public static final String ROOT = "";

  /** Return the name of the command {@code name} is a subcommand of. */
  public static String parentOf(final String name) {
    final int sp = name.lastIndexOf(' ');
    return sp < 0 ? ROOT : name.substring(0, sp);
  }

  /** Return the name of this command, without any of its parents. */
  public static String baseNameOf(final String name) {
    return name.substring(name.lastIndexOf(' ') + 1);
  }

  /** Is the second command a direct child of the first command? */
  public static boolean isChild(final String parent, final String name) {
    return !name.isEmpty() && parent.equals(parentOf(name));
  }

  private Commands() {
  }
}
