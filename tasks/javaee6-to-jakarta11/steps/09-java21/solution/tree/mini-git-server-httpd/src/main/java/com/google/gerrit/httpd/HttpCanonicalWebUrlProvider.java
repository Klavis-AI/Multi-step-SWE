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

import com.google.gerrit.server.config.CanonicalWebUrl;
import com.google.gerrit.server.config.CanonicalWebUrlProvider;
import com.google.gerrit.server.config.GerritServerConfig;

import org.eclipse.jgit.lib.Config;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.beans.factory.annotation.Autowired;

import jakarta.servlet.http.HttpServletRequest;

/** Sets {@link CanonicalWebUrl} to current HTTP request if not configured. */
public class HttpCanonicalWebUrlProvider extends CanonicalWebUrlProvider {
  private ObjectProvider<HttpServletRequest> requestProvider;

  @Autowired
  HttpCanonicalWebUrlProvider(@GerritServerConfig final Config config) {
    super(config);
  }

  /**
   * Optional: only a web application context can supply the request, and the
   * core server beans are created in a context that has no HTTP environment.
   */
  @Autowired(required = false)
  public void setHttpServletRequest(final ObjectProvider<HttpServletRequest> hsr) {
    requestProvider = hsr;
  }

  @Override
  public String get() {
    String canonicalUrl = super.get();
    if (canonicalUrl != null) {
      return canonicalUrl;
    }

    if (requestProvider != null) {
      // No canonical URL configured? Maybe we can get a reasonable
      // guess from the incoming HTTP request, if we are currently
      // inside of an HTTP request scope.
      //
      final HttpServletRequest req = requestProvider.getIfAvailable();
      if (req == null) {
        return null;
      }

      // Every read of the request is inside the catch, not just the first:
      // the object handed out here is a proxy that stands in for whichever
      // request the calling thread is serving, and each of its methods is the
      // one that discovers there is no such request.
      //
      try {
        // Both halves of the path, for the reason RequireSslFilter spells
        // out: what is being cut off here is everything below the application,
        // and the container hands that back in two pieces.
        final String pathInfo = req.getPathInfo();
        final String path = pathInfo != null
            ? req.getServletPath() + pathInfo : req.getServletPath();
        // See RequireSslFilter: the buffer getRequestURL returns is ours, so
        // it does not need to be a synchronized one.
        final StringBuilder url = new StringBuilder(req.getRequestURL());
        url.setLength(url.length() - path.length());
        if (url.charAt(url.length() - 1) != '/') {
          url.append('/');
        }
        return url.toString();
      } catch (IllegalStateException noWeb) {
        // We can't obtain the request as we are not inside of
        // an HTTP request scope. Callers must handle null.
        //
        return null;
      }
    }

    // We have no way of guessing our HTTP url.
    //
    return null;
  }
}
