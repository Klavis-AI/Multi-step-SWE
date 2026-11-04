#!/usr/bin/env bash
# Reference solution, step 2: the x/net/context shim out, honored deadlines and
# a draining shutdown in.
#
# Every edit below is anchored on text that exists in the tree step 1 leaves
# behind; `edit` fails loudly when an anchor is missing rather than writing a
# file that silently did not change.
set -euo pipefail

cd /app
export GO111MODULE=on GOFLAGS=-mod=mod

python3 - <<'PY'
import io
import sys


def edit(path, *pairs):
    """Apply (old, new) replacements to a file; every `old` must occur once."""
    text = io.open(path).read()
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            sys.exit("%s: anchor occurs %d times, expected 1:\n%s" %
                     (path, n, old))
        text = text.replace(old, new)
    io.open(path, "w").write(text)


# -------------------------------------------------------------------------
# 1. The context type. `golang.org/x/net/context` was the 1.6-era spelling of
#    the standard library's package. The revision this module pins,
#    v0.0.0-20161027203154, predates the alias the shim later became: it
#    declares its own `Context` interface, with the same method set as the
#    standard library's. Same method set is what matters here -- every value
#    that satisfied one satisfies the other -- so the swap is the import line
#    and nothing else. Standard-library imports go in their own first group,
#    which is where gofmt and the rest of this repository keep them.
# -------------------------------------------------------------------------
edit("api/types/types_context.go",
     ('import (\n'
      '\tlog "github.com/Sirupsen/logrus"\n'
      '\t"golang.org/x/net/context"\n'
      ')',
      'import (\n'
      '\t"context"\n'
      '\n'
      '\tlog "github.com/Sirupsen/logrus"\n'
      ')'))

edit("api/context/context.go",
     ('import (\n'
      '\t"fmt"\n',
      'import (\n'
      '\t"context"\n'
      '\t"fmt"\n'),
     ('\tgcontext "github.com/gorilla/context"\n'
      '\t"golang.org/x/net/context"\n',
      '\tgcontext "github.com/gorilla/context"\n'))

edit("client/client.go",
     ('import (\n'
      '\tlog "github.com/Sirupsen/logrus"\n'
      '\tgofig "github.com/akutz/gofig/types"\n'
      '\tgocontext "golang.org/x/net/context"\n',
      'import (\n'
      '\tgocontext "context"\n'
      '\n'
      '\tlog "github.com/Sirupsen/logrus"\n'
      '\tgofig "github.com/akutz/gofig/types"\n'))

# -------------------------------------------------------------------------
# 2. The client's one outbound call. `ctxhttp.Do` is the pre-1.7 way to give a
#    request a context; `(*http.Request).WithContext` is the standard-library
#    one, and `http.Client.Do` has honoured the request's context since 1.7.
#    Cancellation and deadlines keep working because this is what ctxhttp
#    itself does now.
# -------------------------------------------------------------------------
edit("api/client/client_http.go",
     ('\t"github.com/akutz/goof"\n'
      '\t"golang.org/x/net/context/ctxhttp"\n',
      '\t"github.com/akutz/goof"\n'),
     ("\tres, err := ctxhttp.Do(ctx, &c.Client, req)\n",
      "\tres, err := c.Do(req.WithContext(ctx))\n"))

# -------------------------------------------------------------------------
# 3. The server's imports: the standard context, and the two packages whose
#    sentinel errors replace the substring match below. `strings` has no other
#    use in this file once the match is gone.
# -------------------------------------------------------------------------
edit("api/server/server.go",
     ('import (\n'
      '\t"fmt"\n'
      '\t"io"\n'
      '\t"os"\n'
      '\t"os/signal"\n'
      '\t"strings"\n'
      '\t"sync"\n'
      '\t"syscall"\n'
      '\t"time"\n'
      '\n'
      '\tgocontext "golang.org/x/net/context"\n'
      '\n'
      '\tlog "github.com/Sirupsen/logrus"\n',
      'import (\n'
      '\tgocontext "context"\n'
      '\t"errors"\n'
      '\t"fmt"\n'
      '\t"io"\n'
      '\t"net"\n'
      '\t"net/http"\n'
      '\t"os"\n'
      '\t"os/signal"\n'
      '\t"sync"\n'
      '\t"syscall"\n'
      '\t"time"\n'
      '\n'
      '\tlog "github.com/Sirupsen/logrus"\n'),

     # -------------------------------------------------------------------
     # 4. What counts as a server failure. `Serve` always ends with an error;
     #    the substring is the 2017 spelling of "the listener was closed
     #    underneath me", which is what the old `Close` produced and nothing
     #    else. A draining shutdown ends `Serve` with http.ErrServerClosed
     #    instead, so the old test would push a fake error at the supervisor
     #    goroutine, which would leave its select and never answer the close
     #    handshake -- a deadlock on every teardown in the repository's own
     #    suite. The sentinels say the same thing without guessing at text.
     # -------------------------------------------------------------------
     ('\t\t\tif err := srv.Serve(); err != nil {\n'
      '\t\t\t\tif !strings.Contains(\n'
      '\t\t\t\t\terr.Error(), "use of closed network connection") {\n'
      '\t\t\t\t\tsrvErrs <- err\n'
      '\t\t\t\t}\n'
      '\t\t\t}\n',
      '\t\t\t// Serve always ends with an error. Shutdown ends it with\n'
      '\t\t\t// http.ErrServerClosed and Close pulls the listener out from\n'
      '\t\t\t// under the accept loop, which surfaces as net.ErrClosed;\n'
      '\t\t\t// neither is a server failure, and only a real one belongs on\n'
      '\t\t\t// srvErrs -- anything else here wakes the supervisor below down\n'
      '\t\t\t// its error branch, and nothing is left to answer closeSignal.\n'
      '\t\t\tif err := srv.Serve(); err != nil &&\n'
      '\t\t\t\t!errors.Is(err, http.ErrServerClosed) &&\n'
      '\t\t\t\t!errors.Is(err, net.ErrClosed) {\n'
      '\t\t\t\tsrvErrs <- err\n'
      '\t\t\t}\n'),

     # -------------------------------------------------------------------
     # 5. The shutdown itself: drain, with a deadline so one wedged request
     #    cannot hold the process open.
     # -------------------------------------------------------------------
     ('func (s *server) close() error {\n'
      '\ts.ctx.Info("shutting down server")\n'
      '\n',
      '// shutdownGrace bounds how long Close waits for in-flight requests to '
      'finish\n'
      '// before the remaining connections are closed.\n'
      'const shutdownGrace = 30 * time.Second\n'
      '\n'
      'func (s *server) close() error {\n'
      '\ts.ctx.Info("shutting down server")\n'
      '\n'
      '\t// Give the endpoints a bounded chance to finish what they are '
      'already\n'
      '\t// serving. Close() drops the listener and lets the process exit cut\n'
      '\t// whatever was still in flight; Shutdown() drains it, and stops '
      'waiting\n'
      '\t// when this deadline expires.\n'
      '\tshutCtx, cancel := gocontext.WithTimeout(s.ctx, shutdownGrace)\n'
      '\tdefer cancel()\n'
      '\n'),
     ('\t\tif err := srv.Close(); err != nil {\n',
      '\t\tif err := srv.Shutdown(shutCtx); err != nil {\n'))

# -------------------------------------------------------------------------
# 6. The endpoint: the two configured timeouts, and the draining stop that
#    `close()` above now calls. `GetInt` is how the rest of this file reads
#    configuration; the keys are whole seconds and default to 300.
# -------------------------------------------------------------------------
edit("api/server/server_http.go",
     ('import (\n'
      '\t"bytes"\n'
      '\t"crypto/tls"\n',
      'import (\n'
      '\t"bytes"\n'
      '\tgocontext "context"\n'
      '\t"crypto/tls"\n'),
     ('\t"os"\n'
      '\t"sync"\n'
      '\n'
      '\tlog "github.com/Sirupsen/logrus"\n',
      '\t"os"\n'
      '\t"sync"\n'
      '\t"time"\n'
      '\n'
      '\tlog "github.com/Sirupsen/logrus"\n'),
     ('\tsrv := &http.Server{Addr: l.Addr().String()}\n',
      '\tsrv := &http.Server{\n'
      '\t\tAddr: l.Addr().String(),\n'
      '\t\t// libstorage.http.readTimeout and .writeTimeout are whole\n'
      '\t\t// seconds; they have existed as configuration keys, with a 300\n'
      '\t\t// default, since before this server honoured either of them.\n'
      '\t\tReadTimeout: time.Duration(\n'
      '\t\t\ts.config.GetInt(types.ConfigHTTPReadTimeout)) * time.Second,\n'
      '\t\tWriteTimeout: time.Duration(\n'
      '\t\t\ts.config.GetInt(types.ConfigHTTPWriteTimeout)) * time.Second,\n'
      '\t}\n'),
     ('// Context returns this server\'s types.\n'
      'func (s *HTTPServer) Context() types.Context {\n',
      '// Shutdown stops the HTTPServer from listening for inbound requests '
      'and waits\n'
      '// for the requests already in flight to finish. It gives up when ctx '
      'is done,\n'
      '// at which point the remaining connections are closed, and returns '
      'ctx\'s\n'
      '// error. Unlike Close, which drops the listener and leaves active '
      'connections\n'
      '// to be cut when the process exits, this drains them.\n'
      'func (s *HTTPServer) Shutdown(ctx gocontext.Context) error {\n'
      '\treturn s.srv.Shutdown(ctx)\n'
      '}\n'
      '\n'
      '// Context returns this server\'s types.\n'
      'func (s *HTTPServer) Context() types.Context {\n'))
PY

# ---------------------------------------------------------------------------
# 7. The pre-Go-1.7 twin. The two files differ by their import line and their
#    build constraint; no toolchain that can build this module selects the
#    `!go1.7` one. Keep the go1.7 body, drop the constraint, drop both names.
# ---------------------------------------------------------------------------
python3 - <<'PY'
import io
import sys

text = io.open("libstorage_go17.go").read()
head = "//go:build go1.7\n\npackage libstorage\n"
if not text.startswith(head):
    sys.exit("libstorage_go17.go does not start with its build constraint")
io.open("libstorage.go", "w").write(text.replace(head, "package libstorage\n", 1))
PY
rm -f libstorage_go17.go libstorage_pre_go17.go

# ---------------------------------------------------------------------------
# 8. `golang.org/x/net` is no longer imported by anything first-party; tidy
#    drops the requirement. Then the checks step 1 established.
# ---------------------------------------------------------------------------
go mod tidy
gofmt -l -s $(find . -name '*.go' -not -path './vendor/*')
go build ./...
go vet ./...
