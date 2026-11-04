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

import java.io.IOException;
import java.util.function.Supplier;

import jakarta.servlet.Filter;
import jakarta.servlet.FilterChain;
import jakarta.servlet.FilterConfig;
import jakarta.servlet.ServletException;
import jakarta.servlet.ServletRequest;
import jakarta.servlet.ServletResponse;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

/** Requires the connection to use SSL, redirects if not. */
class RequireSslFilter implements Filter {
  private final Supplier<String> urlProvider;

  RequireSslFilter(final Supplier<String> urlProvider) {
    this.urlProvider = urlProvider;
  }

  @Override
  public void init(FilterConfig filterConfig) {
  }

  @Override
  public void destroy() {
  }

  @Override
  public void doFilter(final ServletRequest request,
      final ServletResponse response, final FilterChain chain)
      throws IOException, ServletException {
    final HttpServletRequest req = (HttpServletRequest) request;
    final HttpServletResponse rsp = (HttpServletResponse) response;

    if (isSecure(req)) {
      chain.doFilter(request, response);

    } else {
      // If we wanted SSL, but the user didn't come to us over it,
      // force SSL by issuing a protocol redirect. Try to keep the
      // name "localhost" in case this is an SSH port tunnel.
      //
      final String url;
      if (isLocalHost(req)) {
        final StringBuffer b = req.getRequestURL();
        b.replace(0, b.indexOf(":"), "https");
        url = b.toString();

      } else {
        url = urlProvider.get() + pathOf(req);
      }
      rsp.setStatus(HttpServletResponse.SC_MOVED_PERMANENTLY);
      rsp.setHeader("Location", url);
    }
  }

  /**
   * The whole path the request asked for, which is what the redirect has to
   * carry over. It is two values now rather than one: the container matches
   * this application's servlets on a prefix, so the part of the path that
   * picked the servlet and the part left over for it arrive separately.
   */
  private static String pathOf(final HttpServletRequest req) {
    final String servletPath = req.getServletPath();
    final String pathInfo = req.getPathInfo();
    return pathInfo != null ? servletPath + pathInfo : servletPath;
  }

  private static boolean isSecure(final HttpServletRequest req) {
    return "https".equals(req.getScheme()) || req.isSecure();
  }

  private static boolean isLocalHost(final HttpServletRequest req) {
    return "localhost".equals(req.getServerName())
        || "127.0.0.1".equals(req.getServerName());
  }
}
