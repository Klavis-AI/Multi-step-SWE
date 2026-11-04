// Copyright (C) 2008 The Android Open Source Project
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

package com.google.gerrit.server.git;

import com.google.gerrit.server.config.GerritServerConfig;
import com.google.gerrit.server.config.SitePaths;

import org.eclipse.jgit.errors.RepositoryNotFoundException;
import org.eclipse.jgit.lib.Config;
import org.eclipse.jgit.lib.Constants;
import org.eclipse.jgit.lib.Repository;
import org.eclipse.jgit.lib.RepositoryCache;
import org.eclipse.jgit.lib.RepositoryCache.FileKey;
import org.eclipse.jgit.storage.file.WindowCacheConfig;
import org.eclipse.jgit.util.FS;
import org.eclipse.jgit.util.FileUtils;
import org.eclipse.jgit.util.IO;
import org.eclipse.jgit.util.RawParseUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.SmartLifecycle;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import static java.nio.file.StandardCopyOption.REPLACE_EXISTING;


/** Manages Git repositories stored on the local filesystem. */
public class LocalDiskRepositoryManager implements GitRepositoryManager {
  private static final Logger log =
      LoggerFactory.getLogger(LocalDiskRepositoryManager.class);

  private static final String UNNAMED =
      "Unnamed repository; edit this file to name it for gitweb.";

  public static class Lifecycle implements SmartLifecycle {
    /** First up and last down: everything above it reads a repository. */
    private static final int PHASE = 1;

    private final Config cfg;
    private volatile boolean running;

    public Lifecycle(@GerritServerConfig final Config cfg) {
      this.cfg = cfg;
    }

    @Override
    public int getPhase() {
      return PHASE;
    }

    @Override
    public boolean isRunning() {
      return running;
    }

    @Override
    public void start() {
      final WindowCacheConfig c = new WindowCacheConfig();
      c.fromConfig(cfg);
      c.install();
      running = true;
    }

    @Override
    public void stop() {
      running = false;
    }
  }

  private final File basePath;

  LocalDiskRepositoryManager(final SitePaths site,
      @GerritServerConfig final Config cfg) {
    basePath = site.resolve(cfg.getString("gerrit", null, "basePath"));
    log.info("basePath="+basePath);
    if (basePath == null) {
      throw new IllegalStateException("gerrit.basePath must be configured");
    }

    for (File zipFile : zipsUnder(basePath)) {
        File unzippedFolder = new File(removeExtension(zipFile.toString()));
        log.info("Unzipping "+zipFile+" to "+unzippedFolder);
        deleteQuietly(unzippedFolder);
        try (InputStream zin = Files.newInputStream(zipFile.toPath())) {
            unzip(zin, unzippedFolder);
        } catch (Exception e) { throw new RuntimeException((e)); }
    }
  }

  /** @return base directory under which all projects are stored. */
  public File getBasePath() {
    return basePath;
  }

  private File gitDirOf(String name) {
    return new File(getBasePath(), name);
  }

  public Repository openRepository(String name)
      throws RepositoryNotFoundException {
    if (isUnreasonableName(name)) {
      throw new RepositoryNotFoundException("Invalid name: " + name);
    }

    try {
      final FileKey loc = FileKey.lenient(gitDirOf(name), FS.DETECTED);
      return RepositoryCache.open(loc);
    } catch (IOException e1) {
      final RepositoryNotFoundException e2;
      e2 = new RepositoryNotFoundException("Cannot open repository " + name);
      e2.initCause(e1);
      throw e2;
    }
  }

  public Repository createRepository(String name)
      throws RepositoryNotFoundException {
    if (isUnreasonableName(name)) {
      throw new RepositoryNotFoundException("Invalid name: " + name);
    }

    try {
      File dir = FileKey.resolve(gitDirOf(name), FS.DETECTED);
      FileKey loc;
      if (dir != null) {
        // Already exists on disk, use the repository we found.
        //
        loc = FileKey.exact(dir, FS.DETECTED);
      } else {
        // It doesn't exist under any of the standard permutations
        // of the repository name, so prefer the standard bare name.
        //
        if (!name.endsWith(Constants.DOT_GIT_EXT)) {
          name = name + Constants.DOT_GIT_EXT;
        }
        loc = FileKey.exact(new File(basePath, name), FS.DETECTED);
      }
      return RepositoryCache.open(loc, false);
    } catch (IOException e1) {
      final RepositoryNotFoundException e2;
      e2 = new RepositoryNotFoundException("Cannot open repository " + name);
      e2.initCause(e1);
      throw e2;
    }
  }

  private boolean isUnreasonableName(String name) {
    if (name.length() == 0) return true; // no empty paths

    if (name.indexOf('\\') >= 0) return true; // no windows/dos stlye paths
    if (name.charAt(0) == '/') return true; // no absolute paths
    if (new File(name).isAbsolute()) return true; // no absolute paths

    if (name.startsWith("../")) return true; // no "l../etc/passwd"
    if (name.contains("/../")) return true; // no "foo/../etc/passwd"
    if (name.contains("/./")) return true; // "foo/./foo" is insane to ask
    if (name.contains("//")) return true; // windows UNC path can be "//..."

    return false; // is a reasonable name
  }

  /** Every {@code *.zip} file at or below {@code dir}, deepest last. */
  private static List<File> zipsUnder(final File dir) {
    if (!dir.isDirectory()) {
      return new ArrayList<>();
    }
    try (Stream<Path> paths = Files.walk(dir.toPath())) {
      final List<File> found = new ArrayList<>();
      paths.filter(Files::isRegularFile)
          .filter(p -> p.getFileName().toString().endsWith(".zip"))
          .forEach(p -> found.add(p.toFile()));
      return found;
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  /** Drop the extension, if the last dot is in the last path element. */
  private static String removeExtension(final String filename) {
    final int dot = filename.lastIndexOf('.');
    final int sep =
        Math.max(filename.lastIndexOf('/'), filename.lastIndexOf('\\'));
    return dot > sep ? filename.substring(0, dot) : filename;
  }

  /** Delete a file, or a directory and all of its contents. Never throws. */
  private static void deleteQuietly(final File f) {
    final File[] kids = f.listFiles();
    if (kids != null) {
      for (File k : kids) {
        deleteQuietly(k);
      }
    }
    f.delete();
  }

  /** Expand {@code zin} below {@code dir}, keeping each entry's timestamp. */
  private static void unzip(final InputStream zin, final File dir)
      throws IOException {
    final ZipInputStream in = new ZipInputStream(zin);
    for (ZipEntry e; (e = in.getNextEntry()) != null;) {
      final File out = new File(dir, e.getName());
      if (e.isDirectory()) {
        out.mkdirs();
      } else {
        Files.createDirectories(out.toPath().getParent());
        Files.copy(in, out.toPath(), REPLACE_EXISTING);
      }
      if (e.getTime() != -1) {
        out.setLastModified(e.getTime());
      }
    }
  }
}
