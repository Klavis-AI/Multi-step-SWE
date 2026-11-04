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

package com.google.gerrit.server.util;

public final class HostPlatform {
  private static final boolean win32 = computeWin32();

  /** @return true if this JVM is running on a Windows platform. */
  public static final boolean isWin32() {
    return win32;
  }

  private static final boolean computeWin32() {
    // Read directly. `AccessController.doPrivileged` is deprecated for removal
    // and, with no security manager to install since JDK 18, has been a plain
    // call-through for the whole life of this branch.
    final String osDotName = System.getProperty("os.name");
    return osDotName != null
        && osDotName.toLowerCase().indexOf("windows") != -1;
  }

  private HostPlatform() {
  }
}
