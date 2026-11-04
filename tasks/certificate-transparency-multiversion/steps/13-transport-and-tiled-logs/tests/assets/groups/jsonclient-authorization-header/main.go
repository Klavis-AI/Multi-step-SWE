// jsonclient-authorization-header
//
// v1.3.3 adds jsonclient.Options.Authorization: when set, every request the
// JSONClient makes carries it verbatim as an Authorization header. This is
// what lets the preloader and ct_hammer talk to a bearer-token-protected log.
//
// Graded over a real socket: the group runs its own HTTP server on 127.0.0.1
// on a port obtained by binding :0, points the product's client at it, and
// reads the headers the server actually received. No wall clock, no fixture,
// nothing outside loopback.
package main

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"

	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

type recorder struct {
	mu   sync.Mutex
	last http.Header
}

func (r *recorder) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	r.mu.Lock()
	r.last = req.Header.Clone()
	r.mu.Unlock()
	w.Header().Set("Content-Type", "application/json")
	if _, err := w.Write([]byte(`{"tree_size":11,"timestamp":99}`)); err != nil {
		panic(err)
	}
}

func (r *recorder) values(name string) []string {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.last.Values(name)
}

func (r *recorder) get(name string) string {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.last.Get(name)
}

type reply struct {
	TreeSize int64 `json:"tree_size"`
	Ts       int64 `json:"timestamp"`
}

const (
	bearer = "Bearer harbor-ct-bench-token"
	basic  = "Basic aGFyYm9yOmJlbmNo"
	agent  = "ct-go-harbor-bench/1.0"
)

func main() {
	t := vhelp.New()

	rec := &recorder{}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("binding a loopback port: %v", err)
	}
	srv := &httptest.Server{
		Listener: ln,
		Config:   &http.Server{Handler: rec},
	}
	srv.Start()
	defer srv.Close()

	newClient := func(opts jsonclient.Options) *jsonclient.JSONClient {
		c, err := jsonclient.New(srv.URL, srv.Client(), opts)
		if err != nil {
			t.Fatalf("jsonclient.New: %v", err)
		}
		return c
	}

	ctx := context.Background()

	// 1. Control: the server, the client and the JSON round-trip all work, and
	//    with no Authorization option nothing is sent. Without this every
	//    "header present" case below could pass against a broken transport.
	var got reply
	plain := newClient(jsonclient.Options{})
	if _, _, err := plain.GetAndParse(ctx, "/control", nil, &got); err != nil {
		t.Fatalf("GetAndParse against the loopback server: %v", err)
	}
	t.Check("control-round-trip-and-no-header-by-default",
		got.TreeSize == 11 && got.Ts == 99 && len(rec.values("Authorization")) == 0,
		"round-trip gave %+v and Authorization=%q", got, rec.values("Authorization"))

	// 2. A GET carries the option verbatim.
	auth := newClient(jsonclient.Options{Authorization: bearer})
	if _, _, err := auth.GetAndParse(ctx, "/get", nil, &got); err != nil {
		t.Fatalf("GetAndParse with Authorization: %v", err)
	}
	t.Equal("get-carries-the-authorization-header", rec.get("Authorization"), bearer)

	// 3. ... and exactly once. The client adds the header, it must not also
	//    have been duplicated.
	t.Equal("get-sends-exactly-one-authorization-value",
		fmt.Sprint(len(rec.values("Authorization"))), "1")

	// 4. A POST carries it too.
	if _, _, err := auth.PostAndParse(ctx, "/post", map[string]string{"a": "b"}, &got); err != nil {
		t.Fatalf("PostAndParse with Authorization: %v", err)
	}
	t.Equal("post-carries-the-authorization-header", rec.get("Authorization"), bearer)

	// 5. So does the retrying POST, which is the path the log client uses for
	//    add-chain.
	if _, _, err := auth.PostAndParseWithRetry(ctx, "/retry", map[string]string{"a": "b"}, &got); err != nil {
		t.Fatalf("PostAndParseWithRetry with Authorization: %v", err)
	}
	t.Equal("post-with-retry-carries-the-authorization-header", rec.get("Authorization"), bearer)

	// 6. The value is sent exactly as configured: no scheme is prepended, no
	//    encoding is applied.
	other := newClient(jsonclient.Options{Authorization: basic})
	if _, _, err := other.GetAndParse(ctx, "/basic", nil, &got); err != nil {
		t.Fatalf("GetAndParse with a Basic credential: %v", err)
	}
	t.Equal("authorization-value-is-sent-verbatim", rec.get("Authorization"), basic)

	// 7. An empty option still sends no header, on the POST path as well.
	if _, _, err := plain.PostAndParse(ctx, "/post-plain", map[string]string{"a": "b"}, &got); err != nil {
		t.Fatalf("PostAndParse without Authorization: %v", err)
	}
	t.Check("empty-authorization-sends-no-header",
		len(rec.values("Authorization")) == 0,
		"an empty Authorization option still sent %q", rec.values("Authorization"))

	// 8. Authorization and User-Agent are independent: setting both sends both.
	both := newClient(jsonclient.Options{Authorization: bearer, UserAgent: agent})
	if _, _, err := both.GetAndParse(ctx, "/both", nil, &got); err != nil {
		t.Fatalf("GetAndParse with both options: %v", err)
	}
	t.Equal("authorization-and-user-agent-coexist",
		strings.Join([]string{rec.get("Authorization"), rec.get("User-Agent")}, " + "),
		bearer+" + "+agent)

	t.Done()
}
