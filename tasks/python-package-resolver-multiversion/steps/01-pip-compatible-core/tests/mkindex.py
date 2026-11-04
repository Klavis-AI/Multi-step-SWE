#!/usr/bin/env python3
"""Materialise a packse scenario declaration into a static PEP 503 index.

The scenario schema is packse's (astral-sh/packse, Apache-2.0): a declarative
TOML naming a package universe, the root requirements, and the expected
resolution.  This builder turns one such declaration into an offline index:

    <out>/simple/index.html            PEP 503 root page
    <out>/simple/<norm>/index.html     PEP 503 project page
    <out>/files/<dist>                 the distributions themselves

Distributions are built here rather than by packse so that the corpus is
hermetic (no build backend, no network) and byte-reproducible: every archive
member carries a fixed timestamp and the members are emitted in sorted order.
"""

from __future__ import annotations

import argparse
import hashlib
import gzip
import io
import json
import os
import re
import sys
import tarfile
import zipfile
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - 3.10 and earlier
    import tomli as tomllib  # type: ignore

EPOCH = (1980, 1, 1, 0, 0, 0)
DEFAULT_REQUIRES_PYTHON = ">=3.8"


def normalize(name: str) -> str:
    return re.sub(r"[-_.]+", "-", name).lower()


def filename_escape(name: str) -> str:
    return re.sub(r"[-_.]+", "_", name).lower()


def module_name(name: str) -> str:
    return re.sub(r"[^0-9a-z_]", "_", normalize(name).replace("-", "_"))


def _zip_add(zf: zipfile.ZipFile, arcname: str, data: bytes) -> tuple[str, str, int]:
    info = zipfile.ZipInfo(arcname, date_time=EPOCH)
    info.external_attr = 0o644 << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(info, data)
    digest = hashlib.sha256(data).digest()
    import base64

    b64 = base64.urlsafe_b64encode(digest).rstrip(b"=").decode()
    return arcname, f"sha256={b64}", len(data)


def build_metadata(name: str, version: str, meta: dict) -> bytes:
    lines = [
        "Metadata-Version: 2.1",
        f"Name: {name}",
        f"Version: {version}",
        "Summary: " + (meta.get("description") or f"{name} {version}"),
    ]
    requires_python = meta.get("requires_python", DEFAULT_REQUIRES_PYTHON)
    if requires_python:
        lines.append(f"Requires-Python: {requires_python}")
    for extra in sorted(meta.get("extras", {})):
        lines.append(f"Provides-Extra: {extra}")
    for req in meta.get("requires", []) or []:
        lines.append(f"Requires-Dist: {req}")
    for extra in sorted(meta.get("extras", {})):
        for req in meta["extras"][extra]:
            if ";" in req:
                base, marker = req.split(";", 1)
                lines.append(
                    f"Requires-Dist: {base.strip()} ; ({marker.strip()}) and extra == \"{extra}\""
                )
            else:
                lines.append(f"Requires-Dist: {req} ; extra == \"{extra}\"")
    lines.append("")
    lines.append(f"{name} {version}")
    lines.append("")
    return "\n".join(lines).encode()


# PEP 658 serves a distribution's metadata as a `<file>.metadata` sidecar; PEP
# 714 renamed the HTML attribute that advertises it from `data-dist-info-
# metadata` to `data-core-metadata` and told clients to accept both. A version
# opts in with `metadata_attribute`; `detached_requires`, when given, replaces
# `requires` in the sidecar only, so that a resolution driven by the detached
# copy is distinguishable from one driven by the distribution itself.
METADATA_ATTRIBUTES = {
    "dist-info-metadata": ("data-dist-info-metadata",),
    "core-metadata": ("data-core-metadata",),
    "both": ("data-dist-info-metadata", "data-core-metadata"),
}


def detached_metadata(name: str, version: str, meta: dict) -> tuple[bytes | None, tuple[str, ...]]:
    spelling = meta.get("metadata_attribute")
    if not spelling:
        return None, ()
    if spelling not in METADATA_ATTRIBUTES:
        raise SystemExit(f"unknown metadata_attribute {spelling!r} for {name} {version}")
    side = dict(meta)
    if "detached_requires" in meta:
        side["requires"] = meta["detached_requires"]
    return build_metadata(name, version, side), METADATA_ATTRIBUTES[spelling]


def build_wheel(name: str, version: str, meta: dict, tag: str) -> tuple[str, bytes]:
    dist = filename_escape(name)
    distinfo = f"{dist}-{version}.dist-info"
    records: list[tuple[str, str, int]] = []
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        mod = module_name(name)
        records.append(
            _zip_add(
                zf,
                f"{mod}/__init__.py",
                f'__version__ = "{version}"\n'.encode(),
            )
        )
        records.append(_zip_add(zf, f"{distinfo}/METADATA", build_metadata(name, version, meta)))
        wheel_txt = (
            "Wheel-Version: 1.0\n"
            "Generator: harbor-scenario-index 1.0\n"
            "Root-Is-Purelib: true\n"
            + "".join(f"Tag: {t}\n" for t in [tag])
        ).encode()
        records.append(_zip_add(zf, f"{distinfo}/WHEEL", wheel_txt))
        record_lines = [f"{p},{h},{s}" for p, h, s in sorted(records)]
        record_lines.append(f"{distinfo}/RECORD,,")
        _zip_add(zf, f"{distinfo}/RECORD", ("\n".join(record_lines) + "\n").encode())
    return f"{dist}-{version}-{tag}.whl", buf.getvalue()


BACKEND = '''"""In-tree PEP 517 backend: no third-party build requirements."""
import base64, hashlib, os, zipfile

EPOCH = (1980, 1, 1, 0, 0, 0)


def _read(name):
    with open(os.path.join(os.path.dirname(__file__), name), "rb") as fh:
        return fh.read()


def _add(zf, arc, data, records):
    info = zipfile.ZipInfo(arc, date_time=EPOCH)
    info.external_attr = 0o644 << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(info, data)
    b64 = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
    records.append("%s,sha256=%s,%d" % (arc, b64, len(data)))


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    meta = _read("PKG-INFO")
    name = version = None
    for line in meta.decode().splitlines():
        if line.startswith("Name: ") and name is None:
            name = line[6:].strip()
        elif line.startswith("Version: ") and version is None:
            version = line[9:].strip()
    import re as _re

    dist = _re.sub(r"[-_.]+", "_", name).lower()
    mod = _re.sub(r"[^0-9a-z_]", "_", dist)
    distinfo = "%s-%s.dist-info" % (dist, version)
    fname = "%s-%s-py3-none-any.whl" % (dist, version)
    records = []
    with zipfile.ZipFile(os.path.join(wheel_directory, fname), "w") as zf:
        _add(zf, "%s/__init__.py" % mod, ('__version__ = "%s"\\n' % version).encode(), records)
        _add(zf, "%s/METADATA" % distinfo, meta, records)
        _add(
            zf,
            "%s/WHEEL" % distinfo,
            b"Wheel-Version: 1.0\\nGenerator: harbor-scenario-sdist 1.0\\n"
            b"Root-Is-Purelib: true\\nTag: py3-none-any\\n",
            records,
        )
        records.append("%s/RECORD,," % distinfo)
        _add(zf, "%s/RECORD" % distinfo, ("\\n".join(sorted(records)) + "\\n").encode(), [])
    return fname


def build_sdist(sdist_directory, config_settings=None):
    raise NotImplementedError("scenario sdists are terminal")


def get_requires_for_build_wheel(config_settings=None):
    return []
'''


def build_sdist(name: str, version: str, meta: dict) -> tuple[str, bytes]:
    dist = filename_escape(name)
    root = f"{dist}-{version}"
    pkginfo = build_metadata(name, version, meta)
    pyproject = (
        "[build-system]\n"
        'requires = []\n'
        'build-backend = "scenario_backend"\n'
        'backend-path = ["."]\n'
    ).encode()
    members = {
        f"{root}/PKG-INFO": pkginfo,
        f"{root}/pyproject.toml": pyproject,
        f"{root}/scenario_backend.py": BACKEND.encode(),
    }
    buf = io.BytesIO()
    # `mode="w:gz"` stamps the wall clock into the gzip header, so two runs of
    # the builder produce two different sha256 for the same sdist. Drive the
    # gzip layer directly with a pinned mtime: the index must be byte-identical
    # every time it is built, or a verifier that asserts on a hash is asserting
    # on the clock.
    gz = gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0)
    with tarfile.open(fileobj=gz, mode="w") as tf:
        for arc in sorted(members):
            data = members[arc]
            info = tarfile.TarInfo(arc)
            info.size = len(data)
            info.mtime = 315532800  # 1980-01-01
            info.mode = 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            tf.addfile(info, io.BytesIO(data))
    gz.close()
    return f"{root}.tar.gz", buf.getvalue()


def html_escape(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


# PEP 691 defines the JSON form of a project page; PEP 700 adds `versions` and
# a mandatory per-file `size` at api-version 1.1, which is the version the HTML
# pages already declare.  Both listings are written for every project and the
# server picks between them on the client's `Accept` header, so a client that
# reads only one of the two forms still sees everything a probe declares.  The
# detached-metadata key is spelled exactly as the HTML attribute is
# (`dist-info-metadata` before PEP 714, `core-metadata` after), so a probe that
# pins one spelling pins it in both listings.
PEP691_API_VERSION = "1.1"


def _json_file_entry(fname, href, sha, size, requires_python, yanked,
                     sidecar, attr_names):
    entry: dict = {
        "filename": fname,
        "url": href,
        "hashes": {"sha256": sha},
        "size": size,
        "yanked": bool(yanked),
    }
    if requires_python:
        entry["requires-python"] = requires_python
    if sidecar is not None:
        mhash = hashlib.sha256(sidecar).hexdigest()
        for attr in attr_names:
            entry[attr.split("data-", 1)[1]] = {"sha256": mhash}
    return entry


def _json_project_page(name: str, versions: list[str], files: list[dict]) -> str:
    return json.dumps(
        {
            "meta": {"api-version": PEP691_API_VERSION},
            "name": name,
            "versions": sorted(set(versions)),
            "files": sorted(files, key=lambda f: f["filename"]),
        },
        indent=2,
        sort_keys=True,
    ) + "\n"


def build_index(scenario: dict, out: Path, url_prefix: str = "") -> None:
    """Materialise `scenario`'s package universe as a PEP 503 index under `out`."""
    prefix = url_prefix.rstrip("/")
    files = out / "files"
    simple = out / "simple"
    files.mkdir(parents=True, exist_ok=True)
    simple.mkdir(parents=True, exist_ok=True)

    # A project page is written in both forms unless the scenario asks for the
    # JSON one alone: `json_only` suppresses `index.html`, so the only listing
    # that describes any distribution is the PEP 691 one, and a client that
    # does not ask for the JSON media type is served the directory itself and
    # finds nothing in it.
    json_only = bool(scenario.get("json_only", False))

    projects: dict[str, list[tuple]] = {}
    for pkg_name, pkg in sorted(scenario.get("packages", {}).items()):
        entries: list[tuple] = []
        for version, meta in sorted(pkg.get("versions", {}).items()):
            meta = dict(meta or {})
            requires_python = meta.get("requires_python", DEFAULT_REQUIRES_PYTHON)
            yanked = bool(meta.get("yanked", False))
            sidecar, attr_names = detached_metadata(pkg_name, version, meta)
            if meta.get("wheel", True):
                tags = meta.get("wheel_tags") or ["py3-none-any"]
                for tag in tags:
                    fname, data = build_wheel(pkg_name, version, meta, tag)
                    entries.append((fname, data, version, requires_python, yanked, sidecar, attr_names))
            if meta.get("sdist", True):
                fname, data = build_sdist(pkg_name, version, meta)
                entries.append((fname, data, version, requires_python, yanked, None, ()))
        projects[normalize(pkg_name)] = entries

    for norm, entries in sorted(projects.items()):
        pdir = simple / norm
        pdir.mkdir(parents=True, exist_ok=True)
        json_files: list[dict] = []
        json_versions: list[str] = []
        lines = [
            "<!DOCTYPE html>",
            "<html>",
            "  <head>",
            '    <meta name="pypi:repository-version" content="1.1">',
            f"    <title>Links for {html_escape(norm)}</title>",
            "  </head>",
            "  <body>",
        ]
        for fname, data, version, requires_python, yanked, sidecar, attr_names in sorted(
                entries, key=lambda e: e[0]):
            (files / fname).write_bytes(data)
            sha = hashlib.sha256(data).hexdigest()
            attrs = ""
            if requires_python:
                attrs += f' data-requires-python="{html_escape(requires_python)}"'
            if yanked:
                attrs += ' data-yanked=""'
            if sidecar is not None:
                (files / f"{fname}.metadata").write_bytes(sidecar)
                mhash = hashlib.sha256(sidecar).hexdigest()
                for attr in attr_names:
                    attrs += f' {attr}="sha256={mhash}"'
            href = (f"{prefix}/files/{fname}" if prefix
                    else f"../../files/{fname}")
            lines.append(
                f'    <a href="{href}#sha256={sha}"{attrs}>{fname}</a><br/>'
            )
            json_files.append(_json_file_entry(
                fname, href, sha, len(data), requires_python, yanked, sidecar,
                attr_names))
            json_versions.append(version)
        lines += ["  </body>", "</html>", ""]
        if not json_only:
            (pdir / "index.html").write_text("\n".join(lines))
        (pdir / "index.json").write_text(
            _json_project_page(norm, json_versions, json_files))

    root_lines = [
        "<!DOCTYPE html>",
        "<html>",
        "  <head>",
        '    <meta name="pypi:repository-version" content="1.1">',
        "    <title>Simple index</title>",
        "  </head>",
        "  <body>",
    ]
    for norm in sorted(projects):
        target = f"{prefix}/simple/{norm}/" if prefix else f"{norm}/"
        root_lines.append(f'    <a href="{target}">{norm}</a><br/>')
    root_lines += ["  </body>", "</html>", ""]
    (simple / "index.html").write_text("\n".join(root_lines))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--url-prefix", default="",
                    help="root-absolute prefix the index will be served under, "
                         "e.g. /scenario-id; empty means relative hrefs")
    args = ap.parse_args()
    build_index(tomllib.loads(args.scenario.read_text()), args.out, args.url_prefix)
    return 0


if __name__ == "__main__":
    sys.exit(main())
