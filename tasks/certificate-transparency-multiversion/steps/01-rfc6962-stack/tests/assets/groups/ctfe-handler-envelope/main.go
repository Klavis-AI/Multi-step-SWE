// Group ctfe-handler-envelope.
//
// The CTFE's HTTP surface, observed over a real loopback socket on a port
// obtained by binding :0. Every request in this group is answered without the
// Trillian backend being consulted, so the log context is built with a nil
// backend client and no request here can reach it:
//
//   - the entrypoint table SetUpInstance produces (paths, methods, names)
//     and how a prefix is normalised into it;
//   - get-roots, which is served entirely out of the configured root pool;
//   - the RFC 6962 "first == 0" shortcut, answered without a proof request;
//   - method enforcement and the exact error envelope AppHandler writes.
//
// Error strings are graded here only where the message *is* the observable:
// the CTFE writes them into the HTTP response body itself, and HTTP-API.md
// s2.1 fixes that body's "<status text>\n<error>\n" shape. Where the corpus
// gives the CTFE's own wording verbatim (HTTP-API.md s4.2-s4.4, s5) the body
// is compared in full; where the tail of the message comes from the standard
// library (strconv, encoding/base64, encoding/json) only the documented
// prefix, the status and a non-empty remainder are checked.
package main

import (
	"bytes"
	"encoding/base64"
	"encoding/pem"
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"context"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/trillian/monitoring"

	_ "github.com/google/trillian/crypto/keys/pem/proto"

	"ctverify/vhelp"
)

type reply struct {
	status int
	body   string
}

func main() {
	t := vhelp.New()

	scratch, err := ioutil.TempDir("", "envelope")
	if err != nil {
		t.Fatalf("TempDir: %v", err)
	}
	defer os.RemoveAll(scratch)

	tmpl, err := ioutil.ReadFile(vhelp.GroupFile("logconfig.textproto.in"))
	if err != nil {
		t.Fatalf("group logconfig.textproto.in: %v", err)
	}
	cfgPath := filepath.Join(scratch, "log.textproto")
	if err := ioutil.WriteFile(cfgPath, []byte(strings.Replace(string(tmpl), "@FIXTURES@", vhelp.Fixture(), -1)), 0644); err != nil {
		t.Fatalf("write config: %v", err)
	}
	cfgs, err := ctfe.LogConfigFromFile(cfgPath)
	if err != nil || len(cfgs) != 1 {
		t.Fatalf("LogConfigFromFile: n=%d err=%v", len(cfgs), err)
	}

	ctx := context.Background()
	mf := monitoring.InertMetricFactory{}

	handlers, err := ctfe.SetUpInstance(ctx, nil, cfgs[0], 10*time.Second, mf)
	if err != nil || handlers == nil {
		t.Fatalf("SetUpInstance: %v", err)
	}

	// ---- the entrypoint table ----------------------------------------------
	got := make([]string, 0, len(*handlers))
	shape := make([]string, 0, len(*handlers))
	for path, h := range *handlers {
		got = append(got, path)
		shape = append(shape, path+" "+h.Method+" "+string(h.Name))
	}
	sort.Strings(got)
	sort.Strings(shape)

	wantPaths := []string{
		"/bench/ct/v1/add-chain",
		"/bench/ct/v1/add-pre-chain",
		"/bench/ct/v1/get-entries",
		"/bench/ct/v1/get-entry-and-proof",
		"/bench/ct/v1/get-proof-by-hash",
		"/bench/ct/v1/get-roots",
		"/bench/ct/v1/get-sth",
		"/bench/ct/v1/get-sth-consistency",
	}
	t.Equal("handlers-exact-path-set", strings.Join(got, ","), strings.Join(wantPaths, ","))

	wantShape := []string{
		"/bench/ct/v1/add-chain POST AddChain",
		"/bench/ct/v1/add-pre-chain POST AddPreChain",
		"/bench/ct/v1/get-entries GET GetEntries",
		"/bench/ct/v1/get-entry-and-proof GET GetEntryAndProof",
		"/bench/ct/v1/get-proof-by-hash GET GetProofByHash",
		"/bench/ct/v1/get-roots GET GetRoots",
		"/bench/ct/v1/get-sth GET GetSTH",
		"/bench/ct/v1/get-sth-consistency GET GetSTHConsistency",
	}
	t.Equal("handlers-method-and-name-per-path", strings.Join(shape, "\n"), strings.Join(wantShape, "\n"))

	// A prefix with decoration on both ends lands as exactly one leading and no
	// trailing slash.
	decorated := *cfgs[0]
	decorated.Prefix = "deep/pfx/"
	dh, err := ctfe.SetUpInstance(ctx, nil, &decorated, 10*time.Second, mf)
	if err != nil || dh == nil {
		t.Fatalf("SetUpInstance(decorated prefix): %v", err)
	}
	dpaths := make([]string, 0, len(*dh))
	for path := range *dh {
		dpaths = append(dpaths, path)
	}
	sort.Strings(dpaths)
	wantDecorated := make([]string, len(wantPaths))
	for i, p := range wantPaths {
		wantDecorated[i] = strings.Replace(p, "/bench/", "/deep/pfx/", 1)
	}
	t.Equal("handlers-prefix-normalised", strings.Join(dpaths, ","), strings.Join(wantDecorated, ","))

	// ---- serve them over a real loopback socket -----------------------------
	mux := http.NewServeMux()
	for path, h := range *handlers {
		mux.Handle(path, h)
	}
	srv := httptest.NewServer(mux)
	defer srv.Close()

	get := func(path string) reply {
		rsp, err := http.Get(srv.URL + path)
		if err != nil {
			t.Fatalf("GET %s: %v", path, err)
		}
		defer rsp.Body.Close()
		b, _ := ioutil.ReadAll(rsp.Body)
		return reply{rsp.StatusCode, string(b)}
	}
	post := func(path, body string) reply {
		rsp, err := http.Post(srv.URL+path, "application/json", strings.NewReader(body))
		if err != nil {
			t.Fatalf("POST %s: %v", path, err)
		}
		defer rsp.Body.Close()
		b, _ := ioutil.ReadAll(rsp.Body)
		return reply{rsp.StatusCode, string(b)}
	}
	exact := func(id string, r reply, status int, body string) {
		t.Check(id, r.status == status && r.body == body,
			"status=%d body=%q want status=%d body=%q", r.status, r.body, status, body)
	}
	// jsonOK compares a 200 JSON body after dropping one trailing newline.
	// Whether a handler writes its JSON with json.Marshal+Write or with
	// json.NewEncoder decides whether that newline is there, and no page of the
	// corpus fixes the choice, so it is not graded.
	jsonOK := func(id string, r reply, body string) {
		t.Check(id, r.status == http.StatusOK && strings.TrimSuffix(r.body, "\n") == body,
			"status=%d body=%q want status=200 body=%q", r.status, r.body, body)
	}
	// wrapped grades an error body exactly as far as the documents fix it: the
	// status, the CTFE wrapper prefix HTTP-API.md states, and a non-empty
	// remainder. The remainder in these cases is the standard library's own
	// text (strconv, encoding/base64, encoding/json), which HTTP-API.md s4.3
	// and s4.5 explicitly put outside the contract.
	wrapped := func(id string, r reply, status int, prefix string) {
		rest := strings.TrimSuffix(strings.TrimPrefix(r.body, prefix), "\n")
		t.Check(id, r.status == status && strings.HasPrefix(r.body, prefix) && rest != "",
			"status=%d body=%q want status=%d prefix=%q plus a non-empty reason",
			r.status, r.body, status, prefix)
	}

	// get-roots is answered from the configured pool, in configured order.
	b64 := func(rel ...string) string {
		return base64.StdEncoding.EncodeToString(vhelp.ReadFixture(rel...))
	}
	wantRoots := `{"certificates":["` + b64("minted", "bench-root.der") + `","` + b64("minted", "bench-intermediate.der") + `"]}`
	jsonOK("get-roots-200-with-configured-pool", get("/bench/ct/v1/get-roots"), wantRoots)

	// RFC 6962 s4.4: a consistency proof from tree size 0 is empty, and the log
	// answers it without asking the backend anything.
	jsonOK("get-sth-consistency-from-zero-is-empty-proof",
		get("/bench/ct/v1/get-sth-consistency?first=0&second=0"), `{"consistency":[]}`)

	// Method enforcement happens before anything else in AppHandler.
	exact("get-roots-rejects-post-405",
		post("/bench/ct/v1/get-roots", "{}"), http.StatusMethodNotAllowed,
		"Method Not Allowed\nmethod not allowed: POST\n")
	exact("add-chain-rejects-get-405",
		get("/bench/ct/v1/add-chain"), http.StatusMethodNotAllowed,
		"Method Not Allowed\nmethod not allowed: GET\n")
	exact("add-pre-chain-rejects-get-405",
		get("/bench/ct/v1/add-pre-chain"), http.StatusMethodNotAllowed,
		"Method Not Allowed\nmethod not allowed: GET\n")
	exact("get-sth-rejects-post-405",
		post("/bench/ct/v1/get-sth", "{}"), http.StatusMethodNotAllowed,
		"Method Not Allowed\nmethod not allowed: POST\n")

	// Parameter validation, all of it ahead of any backend call.
	wrapped("get-entries-missing-params-400",
		get("/bench/ct/v1/get-entries"), http.StatusBadRequest,
		"Bad Request\nbad range on get-entries request: ")
	exact("get-entries-inverted-range-400",
		get("/bench/ct/v1/get-entries?start=3&end=1"), http.StatusBadRequest,
		"Bad Request\nbad range on get-entries request: start (3) and end (1) is not a valid range\n")
	exact("get-entries-negative-start-400",
		get("/bench/ct/v1/get-entries?start=-1&end=1"), http.StatusBadRequest,
		"Bad Request\nbad range on get-entries request: start (-1) and end (1) parameters must be >= 0\n")
	exact("get-sth-consistency-inverted-range-400",
		get("/bench/ct/v1/get-sth-consistency?first=5&second=2"), http.StatusBadRequest,
		"Bad Request\nfailed to parse consistency range: invalid first, second params: 5 2\n")
	exact("get-proof-by-hash-missing-hash-400",
		get("/bench/ct/v1/get-proof-by-hash?tree_size=5"), http.StatusBadRequest,
		"Bad Request\nget-proof-by-hash: missing / empty hash param for get-proof-by-hash\n")
	wrapped("get-proof-by-hash-unparseable-hash-400",
		get("/bench/ct/v1/get-proof-by-hash?hash=%21%21%21&tree_size=5"), http.StatusBadRequest,
		"Bad Request\nget-proof-by-hash: invalid base64 hash: ")
	exact("get-proof-by-hash-zero-tree-size-400",
		get("/bench/ct/v1/get-proof-by-hash?hash=AAAA&tree_size=0"), http.StatusBadRequest,
		"Bad Request\nget-proof-by-hash: missing or invalid tree_size: 0\n")
	wrapped("get-entry-and-proof-missing-params-400",
		get("/bench/ct/v1/get-entry-and-proof"), http.StatusBadRequest,
		"Bad Request\nfailed to parse get-entry-and-proof params: ")

	// Submission bodies.
	wrapped("add-chain-unparseable-body-400",
		post("/bench/ct/v1/add-chain", "not json"), http.StatusBadRequest,
		"Bad Request\nfailed to parse add-chain body: ")
	exact("add-chain-empty-chain-400",
		post("/bench/ct/v1/add-chain", `{"chain":[]}`), http.StatusBadRequest,
		"Bad Request\nfailed to parse add-chain body: cert chain was empty\n")

	// A chain that does not reach a configured root is refused before the
	// backend is consulted; with a nil backend client, anything that got past
	// admission would crash instead of answering 400.
	unrooted := post("/bench/ct/v1/add-chain", chainBody(t, "chain-unrooted.pem"))
	t.Check("add-chain-unrooted-refused-before-backend",
		unrooted.status == http.StatusBadRequest &&
			strings.HasPrefix(unrooted.body, "Bad Request\nfailed to verify add-chain contents: chain failed to verify: ") &&
			strings.HasSuffix(unrooted.body, "because: x509: certificate signed by unknown authority\n"),
		"status=%d body=%q", unrooted.status, truncate(unrooted.body))
	unrootedPre := post("/bench/ct/v1/add-pre-chain", chainBody(t, "chain-unrooted.pem"))
	t.Check("add-pre-chain-unrooted-refused-before-backend",
		unrootedPre.status == http.StatusBadRequest &&
			strings.HasSuffix(unrootedPre.body, "because: x509: certificate signed by unknown authority\n"),
		"status=%d body=%q", unrootedPre.status, truncate(unrootedPre.body))

	// ---- the admission knobs of CONFIG.md section 2 -------------------------
	// Each one is given its own instance, served on its own mux, and fed the
	// rooted chain whose leaf NotAfter is 2060-01-01. The window and the CA bit
	// are checked ahead of path building, so a build that honours them answers
	// 400 with the wrapper of HTTP-API.md section 5 and a reason that is *not*
	// the path-building failure; a build that ignores them lets the chain
	// through to a nil backend instead. The reason text itself is CTFE's own
	// and is not fixed by any page, so only the documented prefix, the status
	// and the non-empty remainder are graded (SCOPE-DIAGNOSTIC).
	const wrapPrefix = "Bad Request\nfailed to verify add-chain contents: "
	admission := func(id, extra string) {
		srvA, stop := instanceOn(t, ctx, mf, cfgPath, extra)
		defer stop()
		rsp, err := http.Post(srvA+"/bench/ct/v1/add-chain", "application/json",
			strings.NewReader(chainBody(t, "chain-valid.pem")))
		if err != nil {
			t.Check(id, false, "POST add-chain: %v", err)
			return
		}
		defer rsp.Body.Close()
		raw, _ := ioutil.ReadAll(rsp.Body)
		body := string(raw)
		cut := strings.LastIndex(body, "because: ")
		reason := ""
		if cut >= 0 {
			reason = strings.TrimSuffix(body[cut+len("because: "):], "\n")
		}
		t.Check(id, rsp.StatusCode == http.StatusBadRequest &&
			strings.HasPrefix(body, wrapPrefix) && reason != "" &&
			reason != "x509: certificate signed by unknown authority",
			"status=%d because=%q body=%q want 400, prefix %q, and a reason other than the path-building failure",
			rsp.StatusCode, reason, truncate(body), wrapPrefix)
	}
	// 2050-01-01; the leaf's NotAfter is 2060-01-01, so it is at or past the
	// exclusive limit.
	admission("add-chain-refused-when-leaf-not-after-reaches-not_after_limit",
		"not_after_limit: { seconds: 2524608000 }")
	// 2070-01-01; the leaf's NotAfter is before the start of the window.
	admission("add-chain-refused-when-leaf-not-after-precedes-not_after_start",
		"not_after_start: { seconds: 3155760000 }")
	// The leaf has CA:FALSE.
	admission("add-chain-refused-when-accept_only_ca-is-set-and-the-leaf-is-not-a-ca",
		"accept_only_ca: true")

	// Nothing is mounted outside the eight entrypoints.
	// The 404 comes from net/http's own ServeMux, which this driver builds; the
	// status is the product's observable, the body is the standard library's
	// and is not graded.
	unmapped := get("/bench/ct/v1/get-all-the-things")
	t.Check("unmapped-path-under-prefix-404",
		unmapped.status == http.StatusNotFound && unmapped.body != "",
		"status=%d body=%q", unmapped.status, truncate(unmapped.body))

	t.Done()
}

// instanceOn loads the group's config with one extra field spliced into the
// single `config { ... }` block, stands the resulting instance up on its own
// loopback server and returns its base URL and a stop function. The backend
// client is nil here too: every request these instances answer is refused
// before the backend is reached.
func instanceOn(t *vhelp.T, ctx context.Context, mf monitoring.InertMetricFactory, cfgPath, extra string) (string, func()) {
	raw, err := ioutil.ReadFile(cfgPath)
	if err != nil {
		t.Fatalf("read %s: %v", cfgPath, err)
	}
	i := strings.LastIndex(string(raw), "}")
	if i < 0 {
		t.Fatalf("config %s has no closing brace", cfgPath)
	}
	withExtra := string(raw)[:i] + "\n" + extra + "\n}\n"
	path := filepath.Join(filepath.Dir(cfgPath), "extra.textproto")
	if err := ioutil.WriteFile(path, []byte(withExtra), 0644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
	cfgs, err := ctfe.LogConfigFromFile(path)
	if err != nil || len(cfgs) != 1 {
		t.Fatalf("LogConfigFromFile(%q): n=%d err=%v", extra, len(cfgs), err)
	}
	handlers, err := ctfe.SetUpInstance(ctx, nil, cfgs[0], 10*time.Second, mf)
	if err != nil || handlers == nil {
		t.Fatalf("SetUpInstance(%q): %v", extra, err)
	}
	mux := http.NewServeMux()
	for p, h := range *handlers {
		mux.Handle(p, h)
	}
	srv := httptest.NewServer(mux)
	return srv.URL, srv.Close
}

// chainBody renders a PEM chain fixture as an RFC 6962 add-chain request body.
func chainBody(t *vhelp.T, name string) string {
	rest := vhelp.ReadFixture("minted", name)
	var buf bytes.Buffer
	buf.WriteString(`{"chain":[`)
	n := 0
	for {
		var blk *pem.Block
		blk, rest = pem.Decode(rest)
		if blk == nil {
			break
		}
		if n > 0 {
			buf.WriteByte(',')
		}
		buf.WriteByte('"')
		buf.WriteString(base64.StdEncoding.EncodeToString(blk.Bytes))
		buf.WriteByte('"')
		n++
	}
	buf.WriteString(`]}`)
	if n == 0 {
		t.Fatalf("fixture minted/%s holds no PEM blocks", name)
	}
	return buf.String()
}

func truncate(s string) string {
	if len(s) > 160 {
		return s[:160] + "..."
	}
	return s
}
