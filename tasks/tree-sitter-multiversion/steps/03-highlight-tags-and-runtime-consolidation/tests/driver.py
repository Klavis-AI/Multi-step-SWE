#!/usr/bin/env python3
"""Verifier check runner for the tree-sitter-multiversion task.

Runs a manifest of process-level checks against the candidate's `tree-sitter`
binary and prints one JSON object per check to the results file. It never
decides a score: `test.sh` reads the results, maps checks to groups, applies
`reward = gate x score` and writes the reward files.

Every check is an invocation of the candidate binary at its documented process
boundary -- `generate`, `parse`, `test`, `query`, `build` -- and every
observation is normalised before comparison:

  * ANSI colour is stripped (the binary colours `test` output);
  * the `\t<n> ms\t` timing column `parse` prints on its summary line is
    removed, because it is wall clock and nothing may assert on it;
  * trailing whitespace is stripped per line and the trailing newline dropped.

Usage:  driver.py <manifest.json> <assets-dir> <workdir> <binary> <results.json>
"""

import json
import os
import re
import shutil
import subprocess
import sys
import time

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
TIMING = re.compile(r"\t\d+(?:\.\d+)? ms\t")


def norm(text):
    text = ANSI.sub("", text)
    text = TIMING.sub("\t", text)
    lines = [ln.rstrip() for ln in text.replace("\r\n", "\n").split("\n")]
    while lines and lines[-1] == "":
        lines.pop()
    return "\n".join(lines)


def extract_tree(text):
    """Keep only the S-expression parse tree from `parse` stdout.

    Three things share that stream and two of them are not the tree. v0.21.0
    prints the cc-rs chatter for the parser it compiles on the fly (`running:
    "c++" "--version"`, `exit status: 0`, `cargo:warning=...`) BEFORE the tree.
    Every release prints a one-line summary AFTER it, and that line's timing
    columns change shape three times across the ladder: `\t0 ms\t`, then
    `\t   0.04 ms\t   168 bytes/ms\t`, then `\tParse:    0.03 ms\t...`.

    The tree itself is byte-identical at every tag, so grading the tree alone
    is both stable across the ladder and exactly what these checks mean to
    assert.

    The block is delimited by PARENTHESIS DEPTH, not by the shape of the line
    that follows it. `spec/30-tree-output.md` §1 fixes the tree as a balanced
    S-expression whose root opens at column 0 and whose closing parenthesis is
    "the last non-newline character" of the block; `spec/20-cli.md` §3 puts the
    trailing summary line under "What is **not** an interface" -- explicitly
    including "not the presence or absence of the line". So the summary line
    must not be able to influence this function at all. Ending the block on
    depth 0 honours both documents; ending it on "a line starting with neither
    a space nor `(`" would make the summary line's first character
    load-bearing and fail a correct solution that indented it.

    Parens inside a quoted literal (an anonymous MISSING node prints as
    `(MISSING "(" ...)`) do not count toward depth.
    """
    lines = text.split("\n")
    start = None
    for index, line in enumerate(lines):
        if line.startswith("("):
            start = index
            break
    if start is None:
        return ""
    kept = []
    depth = 0
    for line in lines[start:]:
        kept.append(line)
        depth += _net_parens(line)
        if depth <= 0:
            break
    return "\n".join(kept)


def _net_parens(line):
    """Parenthesis balance of one line, ignoring quoted spans."""
    depth = 0
    quote = None
    escaped = False
    for ch in line:
        if quote:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
            continue
        if ch in ('"', "`"):
            quote = ch
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
    return depth


class Runner:
    def __init__(self, binary, workdir, assets, per_check_timeout):
        self.binary = binary
        self.workdir = workdir
        self.assets = assets
        self.timeout = per_check_timeout

    def run(self, args, cwd, extra_env=None, stdin_data=None):
        env = dict(os.environ)
        env["NO_COLOR"] = "1"
        env["TERM"] = "dumb"
        env["CLICOLOR"] = "0"
        env.setdefault("HOME", "/tmp/ts-home")
        # Derived from HOME, never hardcoded. The compiled-parser cache under
        # this directory is keyed by GRAMMAR NAME and not by the binary that
        # built it, so two different release binaries pointed at one cache will
        # serve each other's .so -- which either runs the wrong parser or fails
        # with a bogus "Incompatible language version". In the verifier that
        # cannot happen (one container, one binary), but two release binaries
        # run side by side on one box against a hardcoded path would silently
        # corrupt each other's results.
        env["TREE_SITTER_DIR"] = os.path.join(env["HOME"], ".tree-sitter")
        if extra_env:
            env.update(extra_env)
            # TREE_SITTER_DIR was derived from HOME above. A check that
            # overrides HOME must carry the derived path with it, or it runs
            # against the previous HOME's cache -- exactly the cross-tag
            # corruption the comment above warns about. An explicit
            # TREE_SITTER_DIR supplied by the check still wins.
            if "HOME" in extra_env and "TREE_SITTER_DIR" not in extra_env:
                env["TREE_SITTER_DIR"] = os.path.join(env["HOME"], ".tree-sitter")
        try:
            proc = subprocess.run(
                [self.binary] + args,
                cwd=cwd,
                env=env,
                input=stdin_data,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=self.timeout,
            )
        except subprocess.TimeoutExpired:
            return 124, "", "TIMEOUT"
        except OSError as exc:
            return 127, "", "OSERROR: %s" % exc
        return (
            proc.returncode,
            proc.stdout.decode("utf-8", "replace"),
            proc.stderr.decode("utf-8", "replace"),
        )

    # -- staging ----------------------------------------------------------
    def stage(self, check):
        """Build a fresh scratch tree for one check.

        `stage` maps an asset-relative source to a destination inside the
        scratch directory; a destination of "." copies a fixture parser
        repository's *contents* to the root, which is what every check that
        runs the binary from a parser repo wants. `write` adds inline files
        after the copies, so a check can perturb a shared fixture without
        needing a fixture of its own."""
        name = check["id"].replace("/", "_")
        dest = os.path.join(self.workdir, name)
        shutil.rmtree(dest, ignore_errors=True)
        os.makedirs(dest)
        spec = check.get("stage", {})
        if isinstance(spec, list):
            spec = {src: os.path.basename(src.rstrip("/")) for src in spec}
        for src_rel, dst_rel in spec.items():
            src = os.path.join(self.assets, src_rel)
            if dst_rel in (".", "", "./"):
                if not os.path.isdir(src):
                    raise ValueError("cannot stage file %r at the root" % src_rel)
                for entry in sorted(os.listdir(src)):
                    inner_src = os.path.join(src, entry)
                    inner_dst = os.path.join(dest, entry)
                    if os.path.isdir(inner_src):
                        shutil.copytree(inner_src, inner_dst, symlinks=False)
                    else:
                        shutil.copy2(inner_src, inner_dst)
                continue
            target = os.path.join(dest, dst_rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            if os.path.isdir(src):
                shutil.copytree(src, target, symlinks=False)
            else:
                shutil.copy2(src, target)
        for rel, content in check.get("write", {}).items():
            path = os.path.join(dest, rel)
            os.makedirs(os.path.dirname(path) or dest, exist_ok=True)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(content)
        for rel in check.get("remove", []):
            path = os.path.join(dest, rel)
            if os.path.isdir(path):
                shutil.rmtree(path, ignore_errors=True)
            elif os.path.exists(path):
                os.remove(path)
        return dest

    # -- per-check kinds --------------------------------------------------
    def do_generate(self, check, cwd):
        args = ["generate"] + check.get("generate_args", [])
        code, out, err = self.run(args, cwd, extra_env=check.get("env"))
        obs = {"generate_exit_zero": code == 0}
        if check.get("capture_generate_stderr_class"):
            obs["generate_reports_a_diagnostic"] = bool(norm(err).strip()) and code != 0
        return obs, code, norm(out), norm(err)

    def parse_test_output(self, text):
        """Structured facts from `tree-sitter test`: the names of tests that
        passed and of tests that failed, in encounter order. The renderer's
        tick/cross glyphs and indentation are the documented shape of that
        output; nothing here asserts on wording or on timing."""
        passed, failed = [], []
        for raw in norm(text).split("\n"):
            # From v0.22.6 the renderer numbers each result ("1. ✓ name");
            # earlier releases print the glyph first. Strip the ordinal before
            # looking at the marker so one parser reads both shapes.
            line = re.sub(r"^\d+\.\s*", "", raw.strip())
            if not line:
                continue
            marker = line[0]
            rest = line[1:].strip()
            if marker == "✓":
                passed.append(rest)
            elif marker == "✗":
                failed.append(rest)
        return passed, failed

    def do_test(self, check, cwd):
        args = ["test"] + check.get("test_args", [])
        code, out, err = self.run(args, cwd, extra_env=check.get("env"))
        passed, failed = self.parse_test_output(out + "\n" + err)
        obs = {
            "test_exit_zero": code == 0,
            "tests_passed": sorted(passed),
            "tests_failed": sorted(failed),
        }
        return obs, code, norm(out), norm(err)

    def do_parse(self, check, cwd):
        args = ["parse"] + check.get("parse_args", []) + [check["input"]]
        code, out, err = self.run(args, cwd, extra_env=check.get("env"))
        obs = {"parse_exit_zero": code == 0, "tree": extract_tree(norm(out))}
        return obs, code, norm(out), norm(err)

    def do_query(self, check, cwd):
        args = ["query"] + check.get("query_args", []) + [check["query"], check["input"]]
        code, out, err = self.run(args, cwd, extra_env=check.get("env"))
        obs = {"query_exit_zero": code == 0, "captures": norm(out)}
        return obs, code, norm(out), norm(err)

    def do_build(self, check, cwd):
        args = ["build"] + check.get("build_args", [])
        code, out, err = self.run(args, cwd, extra_env=check.get("env"))
        obs = {"build_exit_zero": code == 0}
        for artefact in check.get("expect_files", []):
            obs["file:" + artefact] = os.path.exists(os.path.join(cwd, artefact))
        return obs, code, norm(out), norm(err)

    def do_cli(self, check, cwd):
        """Generic invocation, for behaviour whose signal is an exit code or a
        diagnostic rather than a tree.

        Everything observed here is deliberately coarse: an exit code, whether
        a named substring is present, or one capture group from a regex. Error
        *wording* is not a documented interface and must never be asserted on;
        what the specs fix is that a particular input is rejected, and which
        term the diagnostic names."""
        args = list(check.get("args", []))
        code, out, err = self.run(args, cwd, extra_env=check.get("env"),
                                  stdin_data=check.get("stdin"))
        stream = check.get("stream", "both")
        text = {"stdout": norm(out), "stderr": norm(err)}.get(
            stream, norm(out) + "\n" + norm(err)
        )
        obs = {"cli_exit_zero": code == 0, "cli_exit_code": code}
        for name, needle in check.get("contains", {}).items():
            obs["contains:" + name] = needle in text
        for name, pattern in check.get("regex", {}).items():
            found = re.search(pattern, text, re.M)
            obs["regex:" + name] = found.group(1) if found else None
        if check.get("capture") == "exact":
            obs["cli_output"] = text
        for rel in check.get("expect_files", []):
            obs["file:" + rel] = os.path.exists(os.path.join(cwd, rel))
        for rel in check.get("expect_absent", []):
            obs["absent:" + rel] = not os.path.exists(os.path.join(cwd, rel))
        return obs, code, norm(out), norm(err)

    def do_artifact(self, check, cwd):
        """Read a documented fact out of a generated artefact."""
        obs = {}
        for probe in check.get("probes", []):
            path = os.path.join(cwd, probe["file"])
            text = ""
            if os.path.exists(path):
                with open(path, "r", encoding="utf-8", errors="replace") as handle:
                    text = handle.read()
            if "regex" in probe:
                found = re.search(probe["regex"], text, re.M)
                obs[probe["name"]] = found.group(1) if found else None
            elif "contains" in probe:
                obs[probe["name"]] = probe["contains"] in text
            elif "json_path" in probe:
                try:
                    value = json.loads(text)
                    for part in probe["json_path"]:
                        value = value[part]
                except Exception:
                    value = None
                obs[probe["name"]] = value
            else:
                obs[probe["name"]] = os.path.exists(path)
        return obs, 0, "", ""

    def do_files(self, check, cwd):
        obs = {}
        for rel in check.get("expect_files", []):
            obs["file:" + rel] = os.path.exists(os.path.join(cwd, rel))
        for rel in check.get("expect_absent", []):
            obs["absent:" + rel] = not os.path.exists(os.path.join(cwd, rel))
        for rel, spec in check.get("expect_text", {}).items():
            path = os.path.join(cwd, rel)
            text = ""
            if os.path.exists(path):
                with open(path, "r", encoding="utf-8", errors="replace") as handle:
                    text = handle.read()
            obs["text:" + rel] = norm(text) if spec == "exact" else (spec in text)
        return obs, 0, "", ""

    STEPS = {
        "generate": do_generate,
        "test": do_test,
        "parse": do_parse,
        "query": do_query,
        "build": do_build,
        "cli": do_cli,
        "artifact": do_artifact,
        "files": do_files,
    }

    def execute(self, check):
        root = self.stage(check)
        cwd = os.path.join(root, check.get("cwd", "."))
        observation = {}
        transcript = []
        for phase in check["steps"]:
            kind = phase["kind"]
            merged = dict(check)
            merged.update(phase)
            phase_cwd = os.path.join(root, phase["cwd"]) if "cwd" in phase else cwd
            obs, code, out, err = self.STEPS[kind](self, merged, phase_cwd)
            prefix = phase.get("prefix", "")
            for key, value in obs.items():
                observation[prefix + key] = value
            transcript.append(
                {"kind": kind, "exit": code, "stdout": out[:4000], "stderr": err[:4000]}
            )
            if phase.get("stop_on_failure") and code != 0:
                break
        if not check.get("keep"):
            shutil.rmtree(root, ignore_errors=True)
        return observation, transcript


def main():
    manifest_path, assets, workdir, binary, results_path = sys.argv[1:6]
    with open(manifest_path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    os.makedirs(workdir, exist_ok=True)
    os.makedirs(os.path.join(os.environ.get("HOME", "/tmp/ts-home"),
                             ".tree-sitter"), exist_ok=True)
    runner = Runner(binary, workdir, assets, manifest.get("per_check_timeout", 300))

    deadline = time.time() + manifest.get("total_budget", 3000)
    results = {}
    for check in manifest["checks"]:
        if time.time() > deadline:
            results[check["id"]] = {
                "ok": False,
                "reason": "aggregate check budget exhausted before this check ran",
            }
            continue
        try:
            observation, transcript = runner.execute(check)
        except Exception as exc:  # a driver defect must not be a silent pass
            results[check["id"]] = {"ok": False, "reason": "driver error: %r" % exc}
            continue
        expected = check["expect"]
        mismatches = [
            key
            for key in sorted(set(expected) | set(observation))
            if expected.get(key, "<<missing>>") != observation.get(key, "<<missing>>")
        ]
        results[check["id"]] = {
            "ok": not mismatches,
            "mismatched_keys": mismatches,
            "observed": observation,
            "transcript": transcript if mismatches else [],
        }

    with open(results_path, "w", encoding="utf-8") as handle:
        json.dump(results, handle, indent=1, sort_keys=True)
    print("checks run: %d, passed: %d" % (len(results), sum(1 for r in results.values() if r["ok"])))


if __name__ == "__main__":
    main()
