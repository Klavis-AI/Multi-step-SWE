#!/usr/bin/env python3
"""Re-derive `data/meta-expected.json` from the pinned tree.

    SETUPTOOLS_USE_DISTUTILS=stdlib PYTHONHASHSEED=0 \
        python3.11 lib/record_meta.py <pinned-tree> > meta-expected.json

<pinned-tree> is `data/pristine.tar.gz` unpacked (the untouched Bento 0.2.0
checkout). Every value is what the pinned tree's own code says about the
distribution -- its `bento.info` parsed by its own `PackageDescription`, its
distutils metadata produced by its own `bento.conv`, and its PKG-INFO written
by its own `write_pkg_info`. The environment is the one `harness.sh`
`_runner_env` pins for every recorded observation; in particular
SETUPTOOLS_USE_DISTUTILS=stdlib, because the PKG-INFO writer borrows from
`distutils` and setuptools' shim would otherwise pick which one.

Output is `json.dump(..., indent=1, sort_keys=True)` plus a newline. Run on
CPython 3.11 (`python:3.11-slim`) against the pinned tree in that environment it
reproduces the shipped file byte for byte.
"""
import io
import json
import os
import sys


def main(argv):
    tree = os.path.abspath(argv[1])
    sys.path[:] = [p for p in sys.path if p not in ("", ".", os.getcwd())]
    sys.path.insert(0, tree)
    os.chdir(tree)

    from bento.core import PackageDescription
    from bento.conv import pkg_to_distutils_meta, write_pkg_info

    pkg = PackageDescription.from_file("bento.info")

    def data_files(section):
        return {"files": list(section.files),
                "source_dir": section.source_dir,
                "target_dir": section.target_dir}

    buf = io.StringIO()
    write_pkg_info(pkg, buf)

    out = {
        "author": pkg.author,
        "author_email": pkg.author_email,
        "classifiers": list(pkg.classifiers),
        "config_py": pkg.config_py,
        "data_files": {name: data_files(s) for name, s in pkg.data_files.items()},
        "description": pkg.description,
        "description_from_file": pkg.description_from_file,
        "distutils_meta": pkg_to_distutils_meta(pkg),
        "executables": {name: [e.module, e.function]
                        for name, e in pkg.executables.items()},
        "extra_source_files": list(pkg.extra_source_files),
        "hook_files": list(pkg.hook_files),
        "keywords": list(pkg.keywords),
        "license": pkg.license,
        "maintainer": pkg.maintainer,
        "maintainer_email": pkg.maintainer_email,
        "meta_template_files": list(pkg.meta_template_files),
        "name": pkg.name,
        "packages": sorted(pkg.packages),
        "pkg_info": buf.getvalue(),
        "platforms": list(pkg.platforms),
        "py_modules": sorted(pkg.py_modules),
        "summary": pkg.summary,
        "url": pkg.url,
        "version": pkg.version,
    }
    json.dump(out, sys.stdout, indent=1, sort_keys=True)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
