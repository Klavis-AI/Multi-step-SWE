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

import org.springframework.boot.context.properties.ConfigurationProperties;

import java.io.File;

/**
 * The settings this server reads from its environment rather than from the
 * site's own {@code gerrit.config}.
 * <p>
 * There is one: the site directory, which is where {@code gerrit.config}
 * itself is found.
 */
@ConfigurationProperties(prefix = "gerrit")
public class GerritProperties {
  /** Directory holding this server's configuration and its repositories. */
  private File sitePath;

  public File getSitePath() {
    return sitePath;
  }

  public void setSitePath(final File sitePath) {
    this.sitePath = sitePath;
  }
}
