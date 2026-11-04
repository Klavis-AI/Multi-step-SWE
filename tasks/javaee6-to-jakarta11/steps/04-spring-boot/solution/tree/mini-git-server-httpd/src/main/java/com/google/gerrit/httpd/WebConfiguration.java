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

import org.springframework.boot.web.servlet.FilterRegistrationBean;
import org.springframework.boot.web.servlet.ServletRegistrationBean;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Import;
import org.springframework.context.annotation.Primary;
import org.springframework.context.annotation.Scope;
import org.springframework.web.context.WebApplicationContext;

import java.io.IOException;
import java.net.SocketAddress;
import java.util.function.Supplier;

import javax.servlet.Filter;
import javax.servlet.http.HttpServlet;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

/**
 * The HTTP half of the server: its request-scoped beans, its servlets and its
 * filters.
 * <p>
 * The servlets and filters are beans here like everything else. Each one is
 * declared wrapped in the registration that says where it belongs in the
 * container's URL space, and the container is handed those registrations
 * while it starts; that is what this deployment has instead of a
 * {@code WEB-INF/web.xml}.
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

  /**
   * Runs after the framework's own filters, the first of which publishes the
   * request that the request-scoped beans below are scoped to.
   */
  @Bean
  FilterRegistrationBean<RequestCleanupFilter> requestCleanupFilter(
      final RequestCleanupFilter filter) {
    return everyRequest("requestCleanup", filter, 0);
  }

  @Bean
  FilterRegistrationBean<RequireSslFilter> requireSslFilter() {
    final FilterRegistrationBean<RequireSslFilter> registration =
        everyRequest("requireSsl", new RequireSslFilter(canonicalUrl), 1);

    // In the chain only when the canonical URL is an https: one, as before;
    // the difference is that the condition is now carried by the registration
    // rather than deciding whether one is made at all.
    registration.setEnabled(wantSSL);
    return registration;
  }

  private static <T extends Filter> FilterRegistrationBean<T> everyRequest(
      final String name, final T filter, final int order) {
    final FilterRegistrationBean<T> registration =
        new FilterRegistrationBean<>(filter);
    registration.setName(name);
    registration.addUrlPatterns("/*");
    registration.setOrder(order);
    return registration;
  }

  @Bean
  ServletRegistrationBean<SshInfoServlet> sshInfoServlet(
      final SshInfoServlet servlet) {
    return serve("ssh_info", servlet, "/ssh_info");
  }

  @Bean
  ServletRegistrationBean<ProjectServlet> projectServlet(
      final ProjectServlet servlet) {
    return serve("project", servlet, "/p/*");
  }

  @Bean
  ServletRegistrationBean<HttpServlet> notFoundServlet() {
    return serve("notFound", notFound(), "/servlet/*");
  }

  private static <T extends HttpServlet> ServletRegistrationBean<T> serve(
      final String name, final T servlet, final String urlPattern) {
    final ServletRegistrationBean<T> registration =
        new ServletRegistrationBean<>(servlet, urlPattern);
    registration.setName(name);
    return registration;
  }

  private static HttpServlet notFound() {
    return new HttpServlet() {
      private static final long serialVersionUID = 1L;

      @Override
      protected void doGet(final HttpServletRequest req,
          final HttpServletResponse rsp) throws IOException {
        rsp.sendError(HttpServletResponse.SC_NOT_FOUND);
      }
    };
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
