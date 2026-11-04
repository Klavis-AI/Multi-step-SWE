// Group client-rsperror.
//
// ct-spec/CLIENT-API.md §1.1 and ct-spec/RELEASE-MAP.md §2: at v1.0.5
// `RspError` appears in package `client` (it moves to `jsonclient` at v1.1.0,
// leaving a type alias behind), carrying the `Err`, the HTTP `StatusCode` and
// the raw response `Body`. TEST_INTERFACE.md §5 lists it as one of the step's
// new identifiers and §5's `_kit` note calls the jsonclient signature change
// deliberate and breaking.
//
// SCOPE-NET: the only traffic is to an `httptest.NewServer`, which binds :0 on
// loopback and is torn down at the end. Nothing leaves the machine.
// SCOPE-TIME: no clock is read; no assertion depends on one.
package main

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"

	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	// 1-3. The struct's three fields, written and read back. They are exported
	// with exactly these names and types at this release; a candidate that
	// moved the type to `jsonclient` without leaving the `client` alias, or
	// that renamed a field, does not compile this group.
	baseErr := errors.New("got HTTP Status \"500 Internal Server Error\"")
	re := client.RspError{Err: baseErr, StatusCode: 500, Body: []byte("boom")}
	t.Check("rsp-error-carries-the-wrapped-error", re.Err == baseErr, "Err=%v", re.Err)
	t.Equal("rsp-error-carries-the-status-code", re.StatusCode, 500)
	t.EqualBytes("rsp-error-carries-the-raw-body", re.Body, []byte("boom"))

	// 4. Error() carries the wrapped error. CLIENT-API.md §1.1 declares
	// `func (e RspError) Error() string` and says nothing about what else it may
	// add, so what is graded is that the wrapped error reaches the caller
	// through it, not that it is the whole of the rendering.
	t.Check("error-renders-the-wrapped-error",
		strings.Contains(re.Error(), baseErr.Error()), "Error()=%q", re.Error())

	// 5. RspError satisfies the error interface on a *value* — CLIENT-API.md
	// §1.1 declares the method on a value receiver, so a candidate that put it
	// on a pointer fails to compile this assignment rather than failing at
	// first use.
	var asErr error = re
	t.Check("rsp-error-is-an-error",
		strings.Contains(asErr.Error(), baseErr.Error()), "Error()=%q", asErr.Error())

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/ct/v1/get-sth") {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusTeapot)
			fmt.Fprint(w, `{"tree_size":7,"timestamp":1500000000000,"sha256_root_hash":"AAAA","tree_head_signature":"AAAA"}`)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusInternalServerError)
		fmt.Fprint(w, `{"error":"backend exploded"}`)
	}))
	defer srv.Close()

	lc, err := client.New(srv.URL, nil, jsonclient.Options{})
	if err != nil {
		t.Fatalf("client.New against the loopback stub: %v", err)
	}

	// 6. A non-200 status from GetSTH comes back as an RspError, and the
	// RspError's StatusCode is the response's, not the wrapped error's.
	sth, err := lc.GetSTH(context.Background())
	if err == nil {
		t.Check("get-sth-reports-a-non-200-as-an-rsp-error", false, "no error; sth=%v", sth)
	} else if rsp, ok := err.(client.RspError); !ok {
		t.Check("get-sth-reports-a-non-200-as-an-rsp-error", false, "error is %T, not client.RspError: %v", err, err)
	} else {
		t.Equal("get-sth-reports-a-non-200-as-an-rsp-error", rsp.StatusCode, http.StatusTeapot)
	}

	// 7. The body the RspError carries is the raw response body, byte for byte
	// and unparsed — even though it is not valid for the response type.
	_, err = lc.GetSTH(context.Background())
	if rsp, ok := err.(client.RspError); ok {
		t.Equal("the-rsp-error-body-is-the-raw-response-body", string(rsp.Body),
			`{"tree_size":7,"timestamp":1500000000000,"sha256_root_hash":"AAAA","tree_head_signature":"AAAA"}`)
	} else {
		t.Check("the-rsp-error-body-is-the-raw-response-body", false, "error is %T: %v", err, err)
	}

	// 8-9. The retrying add-chain path does *not* mint an RspError at this
	// release, and CLIENT-API.md §1.1 now says why: at v1.0.5 only,
	// PostAndParseWithRetry's default arm returns a nil *http.Response
	// alongside the body, so AddChain, AddPreChain and AddJSON report a
	// non-2xx as the plain `got HTTP Status %q` error and never as an
	// RspError -- the wrapper in client only builds one when a response is
	// available. The status is still reported; no body is claimed. From
	// v1.0.10 that arm returns the response and these two cases invert,
	// which is what makes them this release's behaviour, not a later one's.
	_, err = lc.AddChain(context.Background(), nil)
	t.Check("add-chain-reports-a-non-200-without-a-response",
		err != nil && strings.Contains(err.Error(), `got HTTP Status "500 Internal Server Error"`),
		"err=%v", err)
	if _, isRsp := err.(client.RspError); isRsp {
		t.Check("add-chain-does-not-mint-an-rsp-error-with-a-nil-response", false,
			"got an RspError %v from a nil-response path", err)
	} else {
		t.Check("add-chain-does-not-mint-an-rsp-error-with-a-nil-response", true, "")
	}

	// 10-11. The three v1.0.5 request helpers hand the raw body back alongside
	// the response even on success — the other half of the same RELEASE-MAP
	// bullet. A 200 reaches the caller as (response, body, nil) from
	// GetAndParse, and PostAndParse has the same v1.0.5 three-value shape.
	ok := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprint(w, `{"tree_size":7}`)
	}))
	defer ok.Close()
	lc2, err := client.New(ok.URL, nil, jsonclient.Options{})
	if err != nil {
		t.Fatalf("client.New against the second stub: %v", err)
	}
	var parsed struct {
		TreeSize int64 `json:"tree_size"`
	}
	httpRsp, body, err := lc2.GetAndParse(context.Background(), "/x", nil, &parsed)
	t.Check("get-and-parse-returns-the-raw-body", err == nil && httpRsp != nil && string(body) == `{"tree_size":7}` && parsed.TreeSize == 7,
		"err=%v rsp=%v body=%q parsed=%d", err, httpRsp != nil, body, parsed.TreeSize)

	var echo struct {
		TreeSize int64 `json:"tree_size"`
	}
	postRsp, postBody, err := lc2.PostAndParse(context.Background(), "/y", map[string]int{"n": 1}, &echo)
	t.Check("post-and-parse-returns-the-raw-body", err == nil && postRsp != nil && string(postBody) == `{"tree_size":7}` && echo.TreeSize == 7,
		"err=%v rsp=%v body=%q parsed=%d", err, postRsp != nil, postBody, echo.TreeSize)

	t.Done()
}
