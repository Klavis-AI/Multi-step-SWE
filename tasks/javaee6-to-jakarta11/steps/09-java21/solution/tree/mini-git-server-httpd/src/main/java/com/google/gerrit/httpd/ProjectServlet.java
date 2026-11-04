// Copyright (C) 2010 The Android Open Source Project
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

import com.google.gerrit.server.config.CanonicalWebUrl;
import com.google.gerrit.server.git.GitRepositoryManager;
import com.google.gerrit.server.git.TransferConfig;

import org.eclipse.jgit.errors.RepositoryNotFoundException;
import org.eclipse.jgit.http.server.GitServlet;
import org.eclipse.jgit.http.server.resolver.AsIsFileService;
import org.eclipse.jgit.lib.Repository;
import org.eclipse.jgit.storage.pack.PackConfig;
import org.eclipse.jgit.transport.UploadPack;
import org.eclipse.jgit.transport.resolver.RepositoryResolver;
import org.eclipse.jgit.transport.resolver.ServiceNotAuthorizedException;
import org.eclipse.jgit.transport.resolver.ServiceNotEnabledException;
import org.eclipse.jgit.transport.resolver.UploadPackFactory;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;

import java.io.IOException;
import java.util.function.Supplier;

import jakarta.servlet.ServletConfig;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServlet;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

/** Serves Git repositories over HTTP. */
public class ProjectServlet extends GitServlet {
  // HttpServlet is Serializable by inheritance and this servlet is never
  // serialised, but a serialisable class with no declared id is a class whose
  // wire format changes with its bytecode.
  private static final long serialVersionUID = 1L;

  private static final Logger log =
      LoggerFactory.getLogger(ProjectServlet.class);

  private static final String ATT_CONTROL = "Project";

  static String getProject(HttpServletRequest req)
      throws ServiceNotEnabledException {
    String pc = (String) req.getAttribute(ATT_CONTROL);
    if (pc == null) {
      log.error("No " + ATT_CONTROL + " in request", new Exception("here"));
      throw new ServiceNotEnabledException();
    }
    return pc;
  }

  private final transient Supplier<String> urlProvider;

  @Autowired
  ProjectServlet(final Resolver resolver, final Upload upload,
      @CanonicalWebUrl final Supplier<String> urlProvider) {
    this.urlProvider = urlProvider;

    setRepositoryResolver(resolver);
    setAsIsFileService(AsIsFileService.DISABLED);
    setUploadPackFactory(upload);
  }

  @Override
  public void init(ServletConfig config) throws ServletException {
    super.init(config);

    serveRegex("^/(.*?)/?$").with(new HttpServlet() {
      @Override
      protected void doGet(HttpServletRequest req, HttpServletResponse rsp)
          throws IOException {
        String pc;
        try {
          pc = getProject(req);
        } catch (ServiceNotEnabledException e) {
          rsp.sendError(HttpServletResponse.SC_NOT_FOUND);
          return;
        }
        StringBuilder r = new StringBuilder();
        r.append(urlProvider.get());
        rsp.sendRedirect(r.toString());
      }
    });
  }

  static class Resolver implements RepositoryResolver<HttpServletRequest> {
    private final GitRepositoryManager manager;

    @Autowired
    Resolver(GitRepositoryManager manager) {
      this.manager = manager;
    }

    @Override
    public Repository open(HttpServletRequest req, String projectName)
        throws RepositoryNotFoundException, ServiceNotAuthorizedException,
        ServiceNotEnabledException {
      if (projectName.endsWith(".git")) {
        // Be nice and drop the trailing ".git" suffix, which we never keep
        // in our database, but clients might mistakenly provide anyway.
        //
        projectName = projectName.substring(0, projectName.length() - 4);
      }

      if (projectName.startsWith("/")) {
        // Be nice and drop the leading "/" if supplied by an absolute path.
        // We don't have a file system hierarchy, just a flat namespace in
        // the database's Project entities. We never encode these with a
        // leading '/' but users might accidentally include them in Git URLs.
        //
        projectName = projectName.substring(1);
      }
		
      req.setAttribute(ATT_CONTROL, projectName);

      return manager.openRepository(projectName);
    }
  }

  static class Upload implements UploadPackFactory<HttpServletRequest> {
    private final PackConfig packConfig;

    @Autowired
    Upload(final TransferConfig tc) {
      this.packConfig = tc.getPackConfig();
    }

    @Override
    public UploadPack create(HttpServletRequest req, Repository repo)
        throws ServiceNotEnabledException, ServiceNotAuthorizedException {

      // The Resolver above already checked READ access for us.
      //
      UploadPack up = new UploadPack(repo);
      up.setPackConfig(packConfig);
      return up;
    }
  }

}
