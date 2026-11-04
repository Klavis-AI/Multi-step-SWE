#!/usr/bin/env bash
# Reference solution, step 1: GOPATH era -> Go module.
#
# Everything here is a standard command of the Go toolchain or an edit to the
# repository's own source.
set -euo pipefail

cd /app
export GO111MODULE=on GOFLAGS=-mod=mod

# ---------------------------------------------------------------------------
# 1. Retire the glide scaffolding and the vendored tree it managed.
#
# `vendor/` has to go with it: Go 1.17 turns on `-mod=vendor` by itself when a
# `vendor/` directory exists, and a vendor tree with no `vendor/modules.txt` is
# one the toolchain refuses to build against. Keeping it would mean writing and
# maintaining modules.txt by hand for 39 modules; the module graph is the point
# of the step.
# ---------------------------------------------------------------------------
rm -rf vendor glide.yaml glide.lock glide.mk
# The Makefile included the glide fragment; the include would now fail.
python3 - <<'PY'
import io
text = io.open("Makefile").read()
text = text.replace("# include the glide makefile content\ninclude glide.mk\n", "")
io.open("Makefile", "w").write(text)
PY

# ---------------------------------------------------------------------------
# 2. The module. The path is the tree's own import path -- every first-party
#    import already spells it -- and the directive is 1.17, the release that
#    introduced the build-constraint syntax this step adopts. `go mod tidy`
#    resolves the requirements by minimal version selection from the proxy.
# ---------------------------------------------------------------------------
go mod init github.com/codedellemc/libstorage
go mod edit -go=1.17
go mod tidy

# ---------------------------------------------------------------------------
# 3. Build constraints: `// +build` -> `//go:build`.
#
# gofmt writes the `//go:build` line for any file that has only the legacy
# form, so the conversion is the toolchain's own; what is left is to drop the
# superseded lines. Every constrained file keeps its constraint: the set of
# files each platform and tag selects is unchanged.
# ---------------------------------------------------------------------------
mapfile -t TAGGED < <(grep -rl '^// +build' --include='*.go' . | sort)
gofmt -w "${TAGGED[@]}"
python3 - "${TAGGED[@]}" <<'PY'
import io, sys
for path in sys.argv[1:]:
    lines = io.open(path).readlines()
    out, dropped = [], False
    for line in lines:
        if line.startswith("// +build "):
            dropped = True
            continue
        out.append(line)
    if not dropped:
        continue
    # gofmt separates the constraint block from `package` with a blank line;
    # removing the legacy line can leave two. Collapse the leading run.
    while len(out) > 1 and out[0].strip() == "":
        out.pop(0)
    idx = 0
    while idx < len(out) and out[idx].startswith("//"):
        idx += 1
    while idx + 1 < len(out) and out[idx].strip() == "" and out[idx + 1].strip() == "":
        out.pop(idx)
    io.open(path, "w").writelines(out)
PY

# ---------------------------------------------------------------------------
# 4. Source fixes.
# ---------------------------------------------------------------------------
python3 - <<'PY'
import io, re

def sub(path, pairs):
    text = io.open(path).read()
    for old, new in pairs:
        if old not in text:
            raise SystemExit("%s: pattern not found: %r" % (path, old[:60]))
        text = text.replace(old, new)
    io.open(path, "w").write(text)

# 4a. `go vet`: Fprintln on a constant that already ends in a newline. The
#     blank line it produces is part of the program's help output, so the fix
#     has to keep emitting it -- printing the constant with an explicit "\n"
#     says exactly that, and says it to the vet checker too.
sub("cli/lss/lss.go", [
    ('fmt.Fprintln(apitypes.Stderr, hostUsage)',
     'fmt.Fprintf(apitypes.Stderr, "%s\\n", hostUsage)'),
    ('fmt.Fprintln(apitypes.Stderr, logUsage)',
     'fmt.Fprintf(apitypes.Stderr, "%s\\n", logUsage)'),
])

# 4b. `httptest.ResponseRecorder.HeaderMap` is deprecated. Its own deprecation
#     notice points at `Result().Header`, which is *not* the same map: Result
#     snapshots the headers at the moment the handler called WriteHeader and
#     returns a clone, so anything the handler sets afterwards disappears from
#     it. The recorder's `Header()` method -- the one every http.ResponseWriter
#     has -- returns the live map that `HeaderMap` is, so it is the accessor
#     that preserves what these three call sites do: copy the handler's headers
#     onto the real writer, and dump them to the response log.
for path in ("api/server/handlers/handlers_logging.go",
             "api/server/handlers/handlers_schema_validator.go",
             "api/server/httputils/httputils.go"):
    text = io.open(path).read()
    new = text.replace("rec.HeaderMap", "rec.Header()")
    if new == text:
        raise SystemExit("%s: no HeaderMap use" % path)
    io.open(path, "w").write(new)

# 4c. The TLS fixtures were located through $GOPATH. Outside GOPATH that is the
#     empty string and the path silently resolves to /src/..., so the fixtures
#     are no longer found and the conformance suite fails against a server it
#     cannot authenticate. The repository is the anchor now: this file compiles
#     from inside it, so its own location gives the repository root without an
#     environment variable to be missing.
sub("api/tests/tests_config.go", [
    ('''	tlsPath = path.Join(
		os.Getenv("GOPATH"),
		"/src/github.com/codedellemc/libstorage/.tls")''',
     '''	tlsPath = repoTLSPath()'''),
    ('''import (
	"fmt"
	"os"
	"path"
''',
     '''import (
	"fmt"
	"os"
	"path"
	"runtime"
'''),
])
text = io.open("api/tests/tests_config.go").read()
text += '''
// repoTLSPath returns the repository's .tls fixture directory.
//
// The path is derived from this file's own compile-time location, which is
// inside the repository by construction. The GOPATH-era code derived it from
// $GOPATH instead; outside GOPATH there is no such variable to read.
func repoTLSPath() string {
	_, thisFile, _, ok := runtime.Caller(0)
	if !ok {
		return ".tls"
	}
	return path.Join(path.Dir(thisFile), "..", "..", ".tls")
}
'''
io.open("api/tests/tests_config.go", "w").write(text)
PY

# ---------------------------------------------------------------------------
# 5. Formatting, then a build that proves the module is the one being used.
# ---------------------------------------------------------------------------
gofmt -s -w $(find . -name '*.go' -not -path './vendor/*')
go build ./...
go mod verify >/dev/null
