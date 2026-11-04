/*
 * Copyright (C) 2008, Shawn O. Pearce <spearce@spearce.org>
 *
 * (Taken from JGit org.eclipse.jgit.pgm.opt.CmdLineParser.)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * - Neither the name of the Git Development Community nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

package com.google.gerrit.util.cli;

import org.springframework.beans.factory.BeanFactory;
import org.springframework.beans.factory.ObjectProvider;

import org.kohsuke.args4j.Argument;
import org.kohsuke.args4j.CmdLineException;
import org.kohsuke.args4j.IllegalAnnotationError;
import org.kohsuke.args4j.Option;
import org.kohsuke.args4j.OptionDef;
import org.kohsuke.args4j.ParserProperties;
import org.kohsuke.args4j.spi.OptionHandler;
import org.kohsuke.args4j.spi.Setter;

import java.io.Writer;
import java.util.ArrayList;
import java.util.ResourceBundle;

/**
 * Extended command line parser which handles --foo=value arguments.
 * <p>
 * The args4j package does not natively handle --foo=value and instead prefers
 * to see --foo value on the command line. Many users are used to the GNU style
 * --foo=value long option, so we convert from the GNU style format to the
 * args4j style format prior to invoking args4j for parsing.
 */
public class CmdLineParser {
  public interface Factory {
    CmdLineParser create(Object bean);
  }

  private final BeanFactory beanFactory;
  private final MyParser parser;

  /**
   * Creates a new command line owner that parses arguments/options and set them
   * into the given object.
   *
   * @param bean instance of a class annotated by
   *        {@link org.kohsuke.args4j.Option} and
   *        {@link org.kohsuke.args4j.Argument}. this object will receive
   *        values.
   *
   * @throws IllegalAnnotationError if the option bean class is using args4j
   *         annotations incorrectly.
   */
  public CmdLineParser(final BeanFactory beanFactory, final Object bean)
      throws IllegalAnnotationError {
    this.beanFactory = beanFactory;
    this.parser = new MyParser(bean);
  }

//  public void addArgument(Setter<?> setter, Argument a) {
//    parser.addArgument(setter, a);
//  }
//
//  public void addOption(Setter<?> setter, Option o) {
//    parser.addOption(setter, o);
//  }

  public void printSingleLineUsage(Writer w, ResourceBundle rb) {
    parser.printSingleLineUsage(w, rb);
  }

  public void printUsage(Writer out, ResourceBundle rb) {
    parser.printUsage(out, rb);
  }

  public void parseArgument(final String... args) throws CmdLineException {
    final ArrayList<String> tmp = new ArrayList<>(args.length);
    for (int argi = 0; argi < args.length; argi++) {
      final String str = args[argi];
      if (str.equals("--")) {
        while (argi < args.length)
          tmp.add(args[argi++]);
        break;
      }

      if (str.startsWith("--")) {
        final int eq = str.indexOf('=');
        if (eq > 0) {
          tmp.add(str.substring(0, eq));
          tmp.add(str.substring(eq + 1));
          continue;
        }
      }

      tmp.add(str);
    }

    parser.parseArgument(tmp.toArray(new String[tmp.size()]));
  }

  private class MyParser extends org.kohsuke.args4j.CmdLineParser {
    MyParser(final Object bean) {
      // args4j appends "(default: false)" to every boolean option's help text
      // from 2.0.16 onwards. This server's help output is part of its
      // published interface, so the annotation is switched back off rather
      // than letting a library upgrade rewrite what the command prints.
      super(bean, ParserProperties.defaults().withShowDefaults(false));
    }

    // args4j declares this hook with a raw `Setter` and a raw return, so an
    // override cannot name a type argument without ceasing to be an override.
    // The rawness is the library's; the cast below is the price of it.
    @SuppressWarnings({"rawtypes", "unchecked"})
    @Override
    protected OptionHandler createOptionHandler(final OptionDef option,
        final Setter setter) {
      if (isHandlerSpecified(option) || isEnum(setter) || isPrimitive(setter)) {
        return super.createOptionHandler(option, setter);
      }

      // getBeanProvider resolves against ancestor bean factories as well, so
      // the hand-rolled walk up the container hierarchy is no longer needed.
      final ObjectProvider<OptionHandlerFactory<?>> factory =
          beanFactory.getBeanProvider(OptionHandlerUtil.typeFor(setter.getType()));
      final OptionHandlerFactory<?> f = factory.getIfAvailable();
      if (f != null) {
        return f.create(this, option, setter);
      }

      return super.createOptionHandler(option, setter);
    }

    private boolean isHandlerSpecified(final OptionDef option) {
      return option.handler() != OptionHandler.class;
    }

    private boolean isEnum(final Setter<?> setter) {
      return Enum.class.isAssignableFrom(setter.getType());
    }

    private boolean isPrimitive(final Setter<?> setter) {
      return setter.getType().isPrimitive();
    }
  }
}
