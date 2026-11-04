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


package com.google.gerrit.httpd;

import com.google.gerrit.httpd.raw.SshInfoServlet;
import com.google.gerrit.server.AnonymousUser;
import com.google.gerrit.server.CurrentUser;
import com.google.gerrit.server.IdentifiedUser;
import com.google.gerrit.server.RemotePeer;
import com.google.gerrit.server.config.CanonicalWebUrl;
import com.google.gerrit.server.config.GerritRequestConfiguration;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Import;
import org.springframework.context.annotation.Primary;
import org.springframework.context.annotation.Scope;
import org.springframework.web.context.WebApplicationContext;
import org.springframework.web.filter.RequestContextFilter;

import java.net.SocketAddress;
import java.util.function.Supplier;

import javax.servlet.Filter;
import javax.servlet.ServletContext;

/**
 * The HTTP half of the server: its request-scoped beans, its servlets and its
 * filters.
 * <p>
 * A servlet container is not a bean factory, so the beans declared here are
 * handed to it by {@link #install(ServletContext, WebApplicationContext)},
 * which the deployment's {@code WebApplicationInitializer} calls at startup in
 * place of a {@code WEB-INF/web.xml}.
 */
@Configuration(proxyBeanMethods = false)
@Import({GerritRequestConfiguration.class, RequestCleanupFilter.class,
    HttpRemotePeerProvider.class, HttpIdentifiedUserProvider.class,
    ProjectServlet.class, ProjectServlet.Resolver.class,
    ProjectServlet.Upload.class, SshInfoServlet.class})
public class WebConfiguration {
  private final Supplier<String> canonicalUrl;
  private final boolean wantSSL;

  WebConfiguration(@CanonicalWebUrl final Supplier<String> canonicalUrl) {
    final String url = canonicalUrl.get();
    this.canonicalUrl = canonicalUrl;
    this.wantSSL = url != null && url.startsWith("https:");
  }

  /** Install this application's filters and servlets into the container. */
  public void install(final ServletContext servletContext,
      final WebApplicationContext webContext) {
    // First in the chain: it is what publishes the request that the
    // request-scoped beans below are scoped to.
    filter(servletContext, "requestContext", new RequestContextFilter());

    filter(servletContext, "requestCleanup",
        webContext.getBean(RequestCleanupFilter.class));

    if (wantSSL) {
      filter(servletContext, "requireSsl", new RequireSslFilter(canonicalUrl));
    }

    UrlMapping.register(servletContext, webContext);
  }

  private static void filter(final ServletContext servletContext,
      final String name, final Filter filter) {
    servletContext.addFilter(name, filter)
        .addMappingForUrlPatterns(null, false, "/*");
  }

  @Bean
  @RemotePeer
  @Scope(WebApplicationContext.SCOPE_REQUEST)
  SocketAddress remotePeer(final HttpRemotePeerProvider provider) {
    return provider.getObject();
  }

  /**
   * Primary because {@link AnonymousUser} and {@link IdentifiedUser} are both
   * {@link CurrentUser}s; this is the one an unqualified injection point means.
   */
  @Bean
  @Primary
  @Scope(WebApplicationContext.SCOPE_REQUEST)
  CurrentUser currentUser(final AnonymousUser anonymousUser) {
    return anonymousUser;
  }

  @Bean
  @Scope(WebApplicationContext.SCOPE_REQUEST)
  IdentifiedUser identifiedUser(final HttpIdentifiedUserProvider provider) {
    return provider.getObject();
  }
}
