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

import com.google.gerrit.server.config.GerritGlobalConfiguration;
import com.google.gerrit.server.config.ServerConfigConfiguration;
import com.google.gerrit.server.config.SitePath;
import com.google.gerrit.server.ssh.SshInfo;
import com.google.gerrit.sshd.SshConfiguration;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.annotation.AnnotatedBeanDefinitionReader;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.WebApplicationInitializer;
import org.springframework.web.context.support.GenericWebApplicationContext;

import javax.servlet.ServletContext;
import javax.servlet.ServletContextEvent;
import javax.servlet.ServletContextListener;
import java.io.File;

/**
 * Configures the web application environment for Gerrit Code Review.
 * <p>
 * The servlet container finds this class for itself and calls
 * {@link #onStartup(ServletContext)} while the application is starting, which
 * is what this deployment has instead of a {@code WEB-INF/web.xml}.
 */
public class WebAppInitializer implements WebApplicationInitializer {

//	 static {
//    KeyUtil.setEncoderImpl(new StandardKeyEncoder());
//  }
  private static final Logger log =
      LoggerFactory.getLogger(WebAppInitializer.class);

  private AnnotationConfigApplicationContext cfgContext;
  private AnnotationConfigApplicationContext sysContext;
  private AnnotationConfigApplicationContext sshContext;
  private GenericWebApplicationContext webContext;

  /** Binds the site directory this server was started on. */
  @Configuration(proxyBeanMethods = false)
  static class SitePathConfiguration {
    @Bean
    @SitePath
    File sitePath() {
      return new File(System.getProperty("gerrit.site_path"));
    }
  }

  private synchronized void init(final ServletContext servletContext) {
    if (sysContext == null) {
      cfgContext = createCfgContext();
      sysContext = createSysContext();
      sshContext = createSshContext();
      webContext = createWebContext(servletContext);

      // Push the canonical URL provider's request supplier down into it. Its
      // optional for that provider, but since we can supply one we should do
      // so, in case the administrator has not setup the canonical URL in the
      // configuration file.
      //
      // Note we have to do this manually as the container failed to do the
      // injection here because the HTTP environment is not visible
      // to the core server modules.
      //
//      sysContext.getBean(HttpCanonicalWebUrlProvider.class)
//          .setHttpServletRequest(
//              webContext.getBeanProvider(HttpServletRequest.class));
    }
  }

  private AnnotationConfigApplicationContext createCfgContext() {
    final AnnotationConfigApplicationContext context =
        new AnnotationConfigApplicationContext();

	  System.out.println("createCfgContext() - about to add ServerConfigConfiguration");
	context.register(SitePathConfiguration.class, ServerConfigConfiguration.class);

	  
	 System.out.println("Fungle createCfgContext");
    //context.register(AuthConfigConfiguration.class);
    context.refresh();
    return context;
  }

  /**
   * The contexts below come up in the order they are created and go down in
   * the reverse of it, and the beans inside each one that have something to
   * start do the same among themselves: refreshing a context starts them and
   * closing it stops them, so there is no separate pass over them here.
   */
  private AnnotationConfigApplicationContext createSysContext() {
    final AnnotationConfigApplicationContext context =
        new AnnotationConfigApplicationContext();
    context.setParent(cfgContext);
    context.register(GerritGlobalConfiguration.class,
        CanonicalWebUrlConfiguration.class);
    context.refresh();
    return context;
  }

  private AnnotationConfigApplicationContext createSshContext() {
    final AnnotationConfigApplicationContext context =
        new AnnotationConfigApplicationContext();
    context.setParent(sysContext);
    context.register(SshConfiguration.class);
    context.refresh();
    return context;
  }

  private GenericWebApplicationContext createWebContext(
      final ServletContext servletContext) {
    final GenericWebApplicationContext context =
        new GenericWebApplicationContext(servletContext);
    context.setParent(sysContext);

    // The SSH daemon is a sibling of this context rather than an ancestor of
    // it, so the one bean the web half borrows from it is handed over here
    // instead of being inherited.
    context.getBeanFactory().registerSingleton("sshInfo",
        sshContext.getBean(SshInfo.class));

    new AnnotatedBeanDefinitionReader(context).register(WebConfiguration.class);
    context.refresh();
    return context;
  }

  @Override
  public void onStartup(final ServletContext servletContext) {
	System.out.println("WAI onStartup - " + servletContext);
    init(servletContext);
    webContext.getBean(WebConfiguration.class)
        .install(servletContext, webContext);
    servletContext.addListener(new ShutdownListener());
  }

  /** Shuts the server down with the servlet context it was started for. */
  private class ShutdownListener implements ServletContextListener {
    @Override
    public void contextInitialized(final ServletContextEvent event) {
    }

    @Override
    public void contextDestroyed(final ServletContextEvent event) {
	  System.out.println("WAI contextDestroyed - "+event);
      webContext.close();
      sshContext.close();
      sysContext.close();
      cfgContext.close();
    }
  }
}
