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

package com.google.gerrit.server.config;

import org.eclipse.jgit.errors.ConfigInvalidException;
import org.eclipse.jgit.lib.Config;
import org.eclipse.jgit.storage.file.FileBasedConfig;
import org.eclipse.jgit.util.FS;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.BeanCreationException;
import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.annotation.Autowired;

import java.io.IOException;

/** Provides {@link Config} annotated with {@link GerritServerConfig}. */
class GerritServerConfigProvider implements ObjectFactory<Config> {
  private static final Logger log =
      LoggerFactory.getLogger(GerritServerConfigProvider.class);

  private final SitePaths site;

  @Autowired
  GerritServerConfigProvider(final SitePaths site) {
    this.site = site;
  }

  @Override
  public Config getObject() {
    FileBasedConfig cfg = new FileBasedConfig(site.gerrit_config, FS.DETECTED);

    if (!cfg.getFile().exists()) {
      log.info("No " + site.gerrit_config.getAbsolutePath()
          + "; assuming defaults");
      return cfg;
    }

    try {
      cfg.load();
    } catch (IOException | ConfigInvalidException e) {
      throw new BeanCreationException(e.getMessage(), e);
    }

    if (site.secure_config.exists()) {
      cfg = new FileBasedConfig(cfg, site.secure_config, FS.DETECTED);
      try {
        cfg.load();
      } catch (IOException | ConfigInvalidException e) {
        throw new BeanCreationException(e.getMessage(), e);
      }
    }

    return cfg;
  }
}
