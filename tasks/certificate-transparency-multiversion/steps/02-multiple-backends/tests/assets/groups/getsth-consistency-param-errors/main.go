// Group getsth-consistency-param-errors.
//
// v1.0.5 rewrites parseGetSTHConsistencyRange so that get-sth-consistency
// diagnoses its own query string. Before, a missing or non-numeric parameter
// fell through to strconv and the client was handed the Go parser's message,
// which names neither the parameter nor whether it was absent or unparseable.
// From this release the handler distinguishes the two conditions and names the
// offending parameter, and those strings are part of the HTTP response body,
// so they are the client-visible contract.
//
// Everything here is asserted over a real loopback socket against the
// candidate's own ct_server. HTTP-API.md section 4.2 gives all six detail texts
// verbatim -- including the two offending values both range checks append to
// theirs -- and section 2.1 gives the frame they sit in, so each response is
// graded as the whole body (TEST_INTERFACE.md section 6.1, item 1). Section 4.2
// also decides the one case the texts alone leave open: a parameter that is
// present with an empty value is the *required* condition, not the malformed
// one, from v1.0.5 to v1.3.3. Nothing else about the response is graded.
package main

import (
	"encoding/base64"
	"encoding/json"
	"net/http"

	"ctverify/vhelp"
)

// badRequest is the whole 400 body HTTP-API.md section 4.2 fixes: http.Error's
// "<status text>\n<error>\n" frame from section 2.1, the section's own wrapper,
// and one of its six detail texts.
func badRequest(detail string) string {
	return "Bad Request\nfailed to parse consistency range: " + detail + "\n"
}

type consistencyJSON struct {
	Consistency [][]byte `json:"consistency"`
}

func main() {
	t := vhelp.New()

	be := newBackend(backendA, rootNanosA, rootHashA[:], treeSizeA)
	addr := be.serve(t)

	cfg := "backends {\n" +
		"  backend { name: \"" + backendA + "\" backend_spec: \"" + addr + "\" }\n}\n" +
		"log_configs {\n" +
		logConfigBody(t, "  ", logIDA, prefixA, backendA, "") +
		"}\n"
	h := start(t, writeScratch(t, "one.textproto", cfg), []string{"--log_rpc_server="}, prefixA)
	defer h.stop()

	ask := func(query string) (int, string) {
		r := httpGet(t, h.url(prefixA, "/ct/v1/get-sth-consistency"+query))
		return r.status, string(r.body)
	}

	type want struct {
		id, query, detail string
	}
	for _, w := range []want{
		// Absent parameters are reported as absent, by name, first one first.
		{id: "no-parameters-names-the-missing-first", query: "", detail: "parameter 'first' is required"},
		{id: "only-second-given-names-the-missing-first", query: "?second=2", detail: "parameter 'first' is required"},
		{id: "only-first-given-names-the-missing-second", query: "?first=1", detail: "parameter 'second' is required"},
		// A present-but-empty value is the same condition as an absent one,
		// not a parse failure (HTTP-API.md section 4.2, "present-but-empty is
		// required, not malformed").
		{id: "empty-first-value-is-required-not-malformed", query: "?first=&second=2", detail: "parameter 'first' is required"},
		{id: "empty-second-value-is-required-not-malformed", query: "?first=1&second=", detail: "parameter 'second' is required"},
		// A present, unparseable value is reported as malformed, by name.
		{id: "non-numeric-first-is-malformed", query: "?first=abc&second=2", detail: "parameter 'first' is malformed"},
		{id: "non-numeric-second-is-malformed", query: "?first=1&second=xyz", detail: "parameter 'second' is malformed"},
		// The range checks that follow are unchanged and still reported. Both
		// append the two values they rejected (HTTP-API.md section 4.2).
		{id: "negative-parameters-are-refused", query: "?first=-1&second=2", detail: "first and second params cannot be <0: -1 2"},
		{id: "second-below-first-is-refused", query: "?first=5&second=2", detail: "invalid first, second params: 5 2"},
	} {
		status, body := ask(w.query)
		t.Equal(w.id, itoa(int64(status))+"|"+body, "400|"+badRequest(w.detail))
	}

	// A well-formed range still reaches the backend and returns its proof.
	r := httpGet(t, h.url(prefixA, "/ct/v1/get-sth-consistency?first=1&second="+itoa(treeSizeA)))
	if r.status != http.StatusOK {
		t.Check("well-formed-range-returns-the-backend-proof", false,
			"status %d body %q", r.status, string(r.body))
	} else {
		var c consistencyJSON
		if err := json.Unmarshal(r.body, &c); err != nil {
			t.Check("well-formed-range-returns-the-backend-proof", false, "body is not JSON: %v", err)
		} else {
			var got []string
			for _, hsh := range c.Consistency {
				got = append(got, base64.StdEncoding.EncodeToString(hsh))
			}
			t.Equal("well-formed-range-returns-the-backend-proof", got,
				[]string{base64.StdEncoding.EncodeToString(rootHashA[:])})
		}
	}

	t.Done()
}
