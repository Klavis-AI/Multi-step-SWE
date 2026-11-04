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

import com.google.gerrit.server.config.SitePath;

import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import java.io.File;

/** Binds the site directory this server was started on. */
@Configuration(proxyBeanMethods = false)
@EnableConfigurationProperties(GerritProperties.class)
public class SitePathConfiguration {
  @Bean
  @SitePath
  File sitePath(final GerritProperties properties) {
    return properties.getSitePath();
  }
}
