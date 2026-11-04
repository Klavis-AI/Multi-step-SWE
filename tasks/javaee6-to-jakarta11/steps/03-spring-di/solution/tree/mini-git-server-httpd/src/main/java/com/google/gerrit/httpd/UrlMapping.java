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

import org.springframework.web.context.WebApplicationContext;

import java.io.IOException;

import javax.servlet.ServletContext;
import javax.servlet.http.HttpServlet;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

/** Maps this application's URL space onto the servlet container. */
class UrlMapping {
  static void register(final ServletContext servletContext,
      final WebApplicationContext webContext) {
    serve(servletContext, "ssh_info", "/ssh_info",
        webContext.getBean(SshInfoServlet.class));

    serve(servletContext, "project", "/p/*",
        webContext.getBean(ProjectServlet.class));

    serve(servletContext, "notFound", "/servlet/*", notFound());
  }

  private static void serve(final ServletContext servletContext,
      final String name, final String urlPattern, final HttpServlet servlet) {
    servletContext.addServlet(name, servlet).addMapping(urlPattern);
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

  private static HttpServlet screen(final String target) {
    return new HttpServlet() {
      private static final long serialVersionUID = 1L;

      @Override
      protected void doGet(final HttpServletRequest req,
          final HttpServletResponse rsp) throws IOException {
        toGerrit(target, req, rsp);
      }
    };
  }

  private static HttpServlet legacyGerritScreen() {
    return new HttpServlet() {
      private static final long serialVersionUID = 1L;

      @Override
      protected void doGet(final HttpServletRequest req,
          final HttpServletResponse rsp) throws IOException {
        final String token = req.getPathInfo().substring(1);
        toGerrit(token, req, rsp);
      }
    };
  }

  private static void toGerrit(final String target, final HttpServletRequest req,
      final HttpServletResponse rsp) throws IOException {
    final StringBuilder url = new StringBuilder();
    url.append(req.getContextPath());
    url.append('/');
    url.append('#');
    url.append(target);
    rsp.sendRedirect(url.toString());
  }

  private UrlMapping() {
  }
}
