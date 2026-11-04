// Observes the client-side guard this release adds to the upload path: a POST
// that a redirect turned into some other request method is an error, not a
// success (PROFILE.md, TEST_INTERFACE.md section 6).
//
// The point of the guard is that the conversion is silent. HTTP 301, 302 and
// 303 all turn a POST into a GET, the GET succeeds, and a client that only
// looked at the status would report a submission accepted that the server
// never saw as a submission. 307 and 308 preserve the method, so those must
// still go through -- a client that refused every redirect would be just as
// wrong, and that is why both halves are graded here.
//
// SCOPE-NET: the only server is an in-process httptest listener on loopback
// with an ephemeral port. Nothing is dialled that this process did not start,
// and the listener is closed before the driver exits.
package main

import (
	"context"
	"net/http"
	"net/http/httptest"

	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

// reply is the shape the fake endpoint answers with; the marker records which
// method actually reached it, so a converted request is visible in the body as
// well as in the guard.
type reply struct {
	Marker string `json:"marker"`
}

func main() {
	t := vhelp.New()

	mux := http.NewServeMux()
	mux.HandleFunc("/ct/v1/final", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"marker":"reached-by-` + r.Method + `"}`))
	})
	for path, code := range map[string]int{
		"/ct/v1/moved":     http.StatusMovedPermanently,  // 301
		"/ct/v1/found":     http.StatusFound,             // 302
		"/ct/v1/seeother":  http.StatusSeeOther,          // 303
		"/ct/v1/temporary": http.StatusTemporaryRedirect, // 307
		"/ct/v1/permanent": http.StatusPermanentRedirect, // 308
	} {
		status := code
		mux.HandleFunc(path, func(w http.ResponseWriter, r *http.Request) {
			http.Redirect(w, r, "/ct/v1/final", status)
		})
	}

	srv := httptest.NewServer(mux)
	defer srv.Close()

	c, err := jsonclient.New(srv.URL, nil, jsonclient.Options{})
	if err != nil {
		t.Fatalf("jsonclient.New: %v", err)
	}
	ctx := context.Background()

	// post returns the guard's verdict for one path: whether an error came
	// back, and what the response structure holds afterwards.
	post := func(path string) (error, reply) {
		var got reply
		_, _, err := c.PostAndParse(ctx, path, map[string]string{"submitted": "yes"}, &got)
		return err, got
	}

	// The control: an endpoint that does not redirect is parsed as it always
	// was, so nothing below can pass because posting is broken outright.
	err, got := post("/ct/v1/final")
	t.Check("post-without-a-redirect-parses",
		err == nil && got.Marker == "reached-by-POST",
		"PostAndParse to a plain endpoint = (%v, %+v), want (<nil>, reached-by-POST)", err, got)

	// The three statuses that convert the method.
	err, _ = post("/ct/v1/moved")
	t.Check("post-converted-by-301-is-refused", err != nil,
		"PostAndParse through a 301 returned no error")

	err, _ = post("/ct/v1/found")
	t.Check("post-converted-by-302-is-refused", err != nil,
		"PostAndParse through a 302 returned no error")

	err, converted := post("/ct/v1/seeother")
	t.Check("post-converted-by-303-is-refused", err != nil,
		"PostAndParse through a 303 returned no error")

	// A refusal must be a refusal: the caller's structure is left alone rather
	// than filled in from the body of the request the server actually served.
	t.Check("refused-redirect-leaves-the-response-unparsed", converted.Marker == "",
		"a refused 303 still populated the response with %q", converted.Marker)

	// The two statuses that preserve the method must still succeed.
	err, got = post("/ct/v1/temporary")
	t.Check("post-preserved-by-307-parses",
		err == nil && got.Marker == "reached-by-POST",
		"PostAndParse through a 307 = (%v, %+v), want (<nil>, reached-by-POST)", err, got)

	err, got = post("/ct/v1/permanent")
	t.Check("post-preserved-by-308-parses",
		err == nil && got.Marker == "reached-by-POST",
		"PostAndParse through a 308 = (%v, %+v), want (<nil>, reached-by-POST)", err, got)

	t.Done()
}
