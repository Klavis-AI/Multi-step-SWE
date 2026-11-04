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

import com.google.gerrit.httpd.WebConfiguration;

import org.springframework.boot.SpringBootConfiguration;
import org.springframework.boot.autoconfigure.EnableAutoConfiguration;
import org.springframework.context.annotation.Import;

/**
 * The web half of the server: the only one of the four contexts that needs a
 * servlet container, and so the only one that is auto-configured.
 * <p>
 * Auto-configuration is what supplies the container, the request scope the
 * beans in {@link WebConfiguration} are scoped to and the filter that opens
 * it, so none of that is declared here. What is left to declare, this
 * application declares by hand: there is no {@code @ComponentScan} (and
 * therefore no {@code @SpringBootApplication}, which implies one), because the
 * beans are named explicitly and a scan of this package would drag the
 * ancestor contexts' configuration into this one.
 */
@SpringBootConfiguration(proxyBeanMethods = false)
@EnableAutoConfiguration
@Import(WebConfiguration.class)
public class WebApplication {
}
