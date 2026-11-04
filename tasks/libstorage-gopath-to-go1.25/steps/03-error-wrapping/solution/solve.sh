#!/usr/bin/env bash
# Reference solution, step 3: goof retired, causes made reachable, and the HTTP
# status of an error decided by the error.
#
# The shape of the change
# -----------------------
# `github.com/akutz/goof` supplies four things this repository depends on: an
# error with a message, an error with a bag of named fields, a JSON rendering
# of both, and the HTTP error envelope the server writes and the client reads
# back. What it does not supply is a way to walk to the error a goof was built
# around, which is why `errors.Is` and `errors.As` answer false on every error
# in this tree and why the code that has to know what an error is compares
# values instead of asking questions.
#
# So the library is replaced rather than deleted: section 1 is those same four
# things with an `Unwrap` on them. Three details of it are load-bearing rather
# than decorative, and each one is there because a recorded response says so.
#
#   * The cause lives in the fields map under `inner`, not in a hidden member.
#     goof put it there and the wire format names it, so an error built around
#     a cause renders with an `inner` field -- for a person reading a log, for
#     a client decoding a response, and for the asynchronous task record that
#     marshals a failed task's error into JSON of its own.
#
#   * A cause with nothing of its own to say in JSON renders as its text. An
#     error from `errors.New` marshals to `{}` -- it has no exported members --
#     and `{}` tells a reader nothing about what went wrong. goof rendered such
#     a cause as its message; `renderable` does the same.
#
#   * `*HTTPError` deliberately has no `HTTPStatus` method, so it is not an
#     `HTTPStatusError`. A client turns an error response back into one of
#     these, and a server that is itself the client of another libStorage puts
#     that reconstructed error back on the wire. If it answered for its own
#     status the chained server would report the far server's 404 as its own;
#     the recorded envelopes say the near server reports a 500.
#
# The status filter in section 4 asks `errors.As` for a `types.HTTPStatusError`,
# so the status belongs to the error type rather than to the filter, and an
# error keeps it through however many layers of context. `*ErrBatchProcess`
# answers 500 even when the not-found that interrupted it is one link further
# down the chain, which is the point of asking the outermost link that has an
# answer rather than the innermost.
#
# Every edit below is anchored on text that exists in the tree step 2 leaves
# behind. `edit` requires its anchor to occur exactly once and `sub` requires
# exactly the stated number of occurrences, so a file that has moved underneath
# this script fails loudly instead of being written back unchanged.
set -euo pipefail

cd /app
export GO111MODULE=on GOFLAGS=-mod=mod

# ---------------------------------------------------------------------------
# 1. The replacement for the library. `Detail` is the exported interface the
#    named error types embed -- an error with fields, a JSON rendering, and an
#    `Unwrap` -- and `HTTPStatusError` is the one the status filter asks for.
#    Everything that builds one is a function with the same name and signature
#    goof used, because the call sites in sections 3 and 8 are otherwise
#    unchanged and because keeping the names keeps the diff about behavior.
# ---------------------------------------------------------------------------
cat > api/types/types_error.go <<'GO'
package types

import (
	"encoding/json"
	"fmt"
	"io"
	"strconv"
	"strings"
)

// innerErrorKey is the field an error stores its cause under. It is a field
// rather than a hidden member because the cause travels with the rest of the
// error's detail when the error is rendered as JSON, and the wire format names
// it.
const innerErrorKey = "inner"

// statusKey is the field an HTTPError stores its status under.
const statusKey = "status"

// Detail is an error that carries structured fields alongside its message.
// The fields are what distinguishes one occurrence of an error from another --
// which volume, which service, which path -- and they are what the API renders
// when it has to describe a failure to a client.
type Detail interface {
	error
	json.Marshaler

	// Fields returns the error's fields. The map belongs to the error rather
	// than to the caller: an error reported through the API is described by
	// the fields it was built with.
	Fields() map[string]interface{}

	// Unwrap returns the error this one was built around, or nil if it was
	// not built around one. It is what lets errors.Is and errors.As reach a
	// cause through the errors that were layered over it.
	Unwrap() error
}

// HTTPStatusError is an error that stands for a particular HTTP status. The
// API answers with the status of the outermost error in a chain that has one,
// so an error that already represents the outcome of an operation keeps its
// own status when something else wraps it.
type HTTPStatusError interface {
	error

	// HTTPStatus returns the HTTP status this error represents.
	HTTPStatus() int
}

// detail is the Detail this package builds. It is unexported because the
// constructors below are the only supported way to make one, and because an
// error's representation is not part of the API -- Detail is.
type detail struct {
	msg string

	// data holds the fields, and the cause under innerErrorKey.
	data map[string]interface{}

	// quietJSON drops the message from the JSON rendering. A response
	// envelope sets it on the error it carries, because the envelope already
	// states the message at its top level and repeating it there would say
	// the same thing twice.
	quietJSON bool
}

// base lets this package recognize an error it built itself. The named error
// types below embed the Detail interface, which does not carry this method, so
// only the errors made by the constructors in this file satisfy it.
type baser interface {
	base() *detail
}

func (e *detail) base() *detail { return e }

// Error returns the error's message. The fields are deliberately left out: the
// message is what a person reads, and the fields are what a program reads.
func (e *detail) Error() string { return e.msg }

// Fields returns the error's fields, including its cause.
func (e *detail) Fields() map[string]interface{} { return e.data }

// Unwrap returns the error this one was built around.
func (e *detail) Unwrap() error {
	err, _ := e.data[innerErrorKey].(error)
	return err
}

// MarshalJSON renders the error as its fields. An error with no fields has
// nothing to render but its message, and renders as that message alone.
func (e *detail) MarshalJSON() ([]byte, error) {
	if len(e.data) == 0 {
		return json.Marshal(e.msg)
	}

	m := make(map[string]interface{}, len(e.data)+1)
	for k, v := range e.data {
		m[k] = renderable(v)
	}
	if !e.quietJSON && e.msg != "" {
		m["msg"] = e.msg
	}

	return json.Marshal(m)
}

// renderable returns the value a field should be rendered as. An error that
// has nothing of its own to say in JSON -- one from errors.New, say, which
// renders as an empty object because it has no exported members -- is rendered
// as its message instead, because an empty object tells the reader nothing
// about what went wrong.
func renderable(v interface{}) interface{} {
	err, ok := v.(error)
	if !ok {
		return v
	}
	if buf, merr := json.Marshal(err); merr == nil && !isEmptyJSON(buf) {
		return err
	}
	return err.Error()
}

func isEmptyJSON(buf []byte) bool {
	return len(buf) < 2 || (buf[0] == '{' && buf[1] == '}')
}

// NewError returns an error with the provided message and no fields.
func NewError(msg string) Detail {
	return WithFieldsE(nil, msg, nil)
}

// WithError returns an error with the provided message, built around the
// provided cause.
func WithError(msg string, cause error) Detail {
	return WithFieldsE(nil, msg, cause)
}

// WithField returns an error with the provided message and a single field.
func WithField(key string, val interface{}, msg string) Detail {
	return WithFieldsE(map[string]interface{}{key: val}, msg, nil)
}

// WithFieldE returns an error with the provided message and a single field,
// built around the provided cause.
func WithFieldE(
	key string, val interface{}, msg string, cause error) Detail {
	return WithFieldsE(map[string]interface{}{key: val}, msg, cause)
}

// WithFields returns an error with the provided message and fields.
func WithFields(fields map[string]interface{}, msg string) Detail {
	return WithFieldsE(fields, msg, nil)
}

// WithFieldsE returns an error with the provided message and fields, built
// around the provided cause. The cause is reachable with errors.Is and
// errors.As, and travels with the fields when the error is rendered.
func WithFieldsE(
	fields map[string]interface{}, msg string, cause error) Detail {
	return withFieldsE(fields, msg, cause)
}

func withFieldsE(
	fields map[string]interface{}, msg string, cause error) *detail {

	if fields == nil {
		fields = map[string]interface{}{}
	}
	if cause != nil {
		fields[innerErrorKey] = cause
	}

	return &detail{msg: msg, data: fields}
}

// detailOf returns err as the detail error a response envelope carries. An
// error this package built is used as it is; an error that carries fields of
// its own contributes them to a new detail error; anything else contributes
// its message.
func detailOf(err error) *detail {
	if err == nil {
		return nil
	}
	if b, ok := err.(baser); ok {
		return b.base()
	}
	if d, ok := err.(Detail); ok {
		return withFieldsE(d.Fields(), d.Error(), nil)
	}
	return withFieldsE(nil, err.Error(), nil)
}

// HTTPError is an error that was, or is about to be, the body of an HTTP error
// response. It pairs a status with the error that explains it.
type HTTPError struct {
	d *detail
}

// NewHTTPError returns an HTTPError that reports err with the provided status.
func NewHTTPError(err error, status int) *HTTPError {
	inner := detailOf(err)
	if inner == nil {
		return &HTTPError{withFieldsE(
			map[string]interface{}{statusKey: status},
			strconv.Itoa(status), nil)}
	}

	inner.quietJSON = true

	return &HTTPError{withFieldsE(
		map[string]interface{}{statusKey: status}, inner.Error(), inner)}
}

// Error returns the message of the error being reported.
func (e *HTTPError) Error() string { return e.d.msg }

// Fields returns the status and the error being reported.
func (e *HTTPError) Fields() map[string]interface{} { return e.d.data }

// Unwrap returns the error being reported.
func (e *HTTPError) Unwrap() error { return e.Inner() }

// Status returns the HTTP status of the response this error describes, or
// zero if it does not have one.
func (e *HTTPError) Status() int {
	if v, ok := e.d.data[statusKey].(int); ok {
		return v
	}
	return 0
}

// Inner returns the error being reported, or nil if the response carried a
// status and nothing else.
func (e *HTTPError) Inner() error {
	err, _ := e.d.data[innerErrorKey].(error)
	return err
}

// MarshalJSON renders the error as an HTTP error response body: the message,
// the status, and the error itself under `error`. The error under `error` is
// rendered without its message, which the body already carries at the top.
func (e *HTTPError) MarshalJSON() ([]byte, error) {
	if len(e.d.data) == 0 {
		return json.Marshal(e.d.msg)
	}

	rest := map[string]interface{}{}
	for k, v := range e.d.data {
		if k != statusKey {
			rest[k] = v
		}
	}

	// Everything but the status belongs under `error`. In the ordinary case
	// that is a single error and it is rendered on its own; a body that
	// arrived with more than that keeps what it arrived with.
	var inner error
	if len(rest) > 0 {
		inner = &detail{data: rest}
		if len(rest) == 1 {
			for _, v := range rest {
				if err, ok := v.(error); ok {
					inner = err
				}
			}
		}
	}

	return json.Marshal(struct {
		Message string `json:"message"`
		Status  int    `json:"status"`
		Inner   error  `json:"error,omitempty"`
	}{
		Message: e.d.msg,
		Status:  e.Status(),
		Inner:   inner,
	})
}

// UnmarshalJSON reads an HTTP error response body back into an HTTPError.
func (e *HTTPError) UnmarshalJSON(data []byte) error {
	m := map[string]interface{}{}
	if err := json.Unmarshal(data, &m); err != nil {
		return err
	}

	e.d = unmarshalDetail(m)

	// A status arrives as a JSON number, which decodes as a float64.
	if v, ok := e.d.data[statusKey].(float64); ok {
		e.d.data[statusKey] = int(v)
	}

	return nil
}

// unmarshalDetail reads a decoded JSON object as a detail error. Whatever the
// object called its message and its cause, the error that comes back names
// them the way this package does; every other member stays a field.
func unmarshalDetail(m map[string]interface{}) *detail {
	var (
		d                = &detail{data: m}
		msgKey, innerKey string
		msgVal, innerVal interface{}
	)

	for k, v := range m {
		switch strings.ToLower(k) {
		case "msg", "message":
			msgKey, msgVal = k, v
		case "inner", "error":
			innerKey, innerVal = k, v
		}
	}

	if msgKey != "" {
		if msg, ok := msgVal.(string); ok {
			d.msg = msg
			delete(m, msgKey)
		}
	}

	if innerKey != "" {
		delete(m, innerKey)
		switch inner := innerVal.(type) {
		case map[string]interface{}:
			m[innerErrorKey] = unmarshalDetail(inner)
		case string:
			m[innerErrorKey] = NewError(inner)
		default:
			m[innerErrorKey] = NewError(fmt.Sprintf("%v", inner))
		}
	}

	return d
}

// DecodeHTTPError reads an HTTP error response body from r.
func DecodeHTTPError(r io.Reader) (*HTTPError, error) {
	e := &HTTPError{}
	if err := json.NewDecoder(r).Decode(e); err != nil {
		return nil, err
	}
	return e, nil
}
GO

python3 - <<'PY'
import io
import sys


def read(path):
    return io.open(path).read()


def write(path, text):
    io.open(path, "w").write(text)


def edit(path, *pairs):
    """Apply (old, new) replacements to a file; every `old` must occur once."""
    text = read(path)
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            sys.exit("%s: anchor occurs %d times, expected 1:\n%s" %
                     (path, n, old))
        text = text.replace(old, new)
    write(path, text)


def sub(path, old, new, count):
    """Replace every `old` in a file; there must be exactly `count` of them."""
    text = read(path)
    n = text.count(old)
    if n != count:
        sys.exit("%s: %r occurs %d times, expected %d" % (path, old, n, count))
    write(path, text.replace(old, new))


# -------------------------------------------------------------------------
# 2. The named error types. Each embedded goof's interface and now embeds
#    this package's, which is the whole of what those declarations have to
#    say. The five that stand for an HTTP status say so with a method, and
#    the statuses are exactly the ones the filter in section 4 assigned
#    before: the switch it replaces is where they came from.
#
#    ErrBatchProcess is the interesting one. It answers 500 even though the
#    error that interrupted the batch is now reachable underneath it and may
#    well be a not-found, because "some of what you asked for could not be
#    produced" is a server-side outcome and not the same answer as the one
#    the first failure would have given on its own. Asking the outermost
#    error that has an answer is what keeps that true.
#
#    ErrSecTokInvalid already carried its cause in an exported member and
#    already had a hand-written Error(); it needs an Unwrap so that member
#    is reachable, and a status like the rest.
# -------------------------------------------------------------------------
edit("api/types/types_errors.go",
     ('import (\n'
      '\t"github.com/akutz/goof"\n'
      ')',
      'import (\n'
      '\t"errors"\n'
      '\t"net/http"\n'
      ')'))
sub("api/types/types_errors.go", "goof.New(", "errors.New(", 3)
sub("api/types/types_errors.go", "struct{ goof.Goof }", "struct{ Detail }", 11)

edit("api/types/types_errors.go",
     ('type ErrBadAdminToken struct{ Detail }\n',
      'type ErrBadAdminToken struct{ Detail }\n'
      '\n'
      '// HTTPStatus returns the status a bad admin token is reported with.\n'
      'func (e *ErrBadAdminToken) HTTPStatus() int {'
      ' return http.StatusUnauthorized }\n'),

     ('type ErrNotFound struct{ Detail }\n',
      'type ErrNotFound struct{ Detail }\n'
      '\n'
      '// HTTPStatus returns the status a missing resource is reported with.\n'
      'func (e *ErrNotFound) HTTPStatus() int { return http.StatusNotFound }\n'),

     ('type ErrMissingLocalDevices struct{ Detail }\n',
      'type ErrMissingLocalDevices struct{ Detail }\n'
      '\n'
      '// HTTPStatus returns the status missing local devices are reported'
      ' with: the\n'
      '// request did not carry them, so the request is what was wrong.\n'
      'func (e *ErrMissingLocalDevices) HTTPStatus() int {\n'
      '\treturn http.StatusBadRequest\n'
      '}\n'),

     ('type ErrMissingInstanceID struct{ Detail }\n',
      'type ErrMissingInstanceID struct{ Detail }\n'
      '\n'
      '// HTTPStatus returns the status a missing instance ID is reported'
      ' with: the\n'
      '// request did not carry one, so the request is what was wrong.\n'
      'func (e *ErrMissingInstanceID) HTTPStatus() int {'
      ' return http.StatusBadRequest }\n'),

     ('type ErrBatchProcess struct{ Detail }\n',
      'type ErrBatchProcess struct{ Detail }\n'
      '\n'
      '// HTTPStatus returns the status an interrupted batch is reported'
      ' with. A batch\n'
      '// says what it says regardless of what stopped it: the caller asked'
      ' for many\n'
      '// objects and the answer is that the server could not produce them,'
      ' which is\n'
      '// not the same answer as the one the first failure would have given'
      ' on its\n'
      '// own.\n'
      'func (e *ErrBatchProcess) HTTPStatus() int {\n'
      '\treturn http.StatusInternalServerError\n'
      '}\n'),

     ('func (e *ErrSecTokInvalid) Error() string {\n'
      '\treturn "invalid security token"\n'
      '}\n',
      'func (e *ErrSecTokInvalid) Error() string {\n'
      '\treturn "invalid security token"\n'
      '}\n'
      '\n'
      '// Unwrap returns the error that caused this one.\n'
      'func (e *ErrSecTokInvalid) Unwrap() error { return e.InnerError }\n'
      '\n'
      '// HTTPStatus returns the status an invalid security token is'
      ' reported with.\n'
      'func (e *ErrSecTokInvalid) HTTPStatus() int {'
      ' return http.StatusUnauthorized }\n'))

# -------------------------------------------------------------------------
# 3. The constructors. Every one of them changes in the same two ways -- the
#    embedded field is named for the new interface, and the builder comes
#    from this repository -- so the file is written out rather than patched
#    a line at a time. The two that take a cause are the ones this milestone
#    is about: `WithFieldE` stores it where `errors.Is` and `errors.As` can
#    find it, and nothing about the call sites has to change for that.
# -------------------------------------------------------------------------
write("api/utils/utils_errors.go", r"""
package utils

import (
	"github.com/codedellemc/libstorage/api/types"
)

// NewUnsupportedForClientTypeError returns a new ErrUnsupportedForClientType
// error.
func NewUnsupportedForClientTypeError(
	clientType types.ClientType, op string) error {
	return &types.ErrUnsupportedForClientType{
		Detail: types.WithFields(map[string]interface{}{
			"clientType": clientType,
			"operation":  op,
		}, "unsupported op for client type")}
}

// NewBadAdminTokenError returns a new ErrBadAdminToken error.
func NewBadAdminTokenError(token string) error {
	return &types.ErrBadAdminToken{
		Detail: types.WithField("token", token, "invalid admin token"),
	}
}

// NewNotFoundError returns a new ErrNotFound error.
func NewNotFoundError(resourceID string) error {
	return &types.ErrNotFound{
		Detail: types.WithField(
			"resourceID", resourceID, "resource not found"),
	}
}

// NewMissingInstanceIDError returns a new ErrMissingInstanceID error.
func NewMissingInstanceIDError(service string) error {
	return &types.ErrMissingInstanceID{
		Detail: types.WithField("service", service, "missing instance ID"),
	}
}

// NewMissingLocalDevicesError returns a new ErrMissingLocalDevices error.
func NewMissingLocalDevicesError(service string) error {
	return &types.ErrMissingLocalDevices{
		Detail: types.WithField("service", service, "missing local devices"),
	}
}

// NewStoreKeyErr returns a new ErrStoreKey error.
func NewStoreKeyErr(key string) error {
	return &types.ErrStoreKey{
		Detail: types.WithField("storeKey", key, "missing store key"),
	}
}

// NewDriverTypeErr returns a new ErrDriverTypeErr error.
func NewDriverTypeErr(expectedType, actualType string) error {
	return &types.ErrDriverTypeErr{
		Detail: types.WithFields(map[string]interface{}{
			"expectedType": expectedType,
			"actualType":   actualType,
		}, "invalid driver type")}
}

// NewBatchProcessErr returns a new ErrBatchProcess error. The error that
// interrupted the batch is the returned error's cause, so a caller that knows
// what to do about a particular failure can still find it.
func NewBatchProcessErr(completed interface{}, err error) error {
	return &types.ErrBatchProcess{Detail: types.WithFieldE(
		"completed", completed, "batch processing error", err)}
}

// NewBadFilterErr returns a new ErrBadFilter error. The error that the filter
// failed to compile with is the returned error's cause.
func NewBadFilterErr(filter string, err error) error {
	return &types.ErrBadFilter{Detail: types.WithFieldE(
		"filter", filter, "bad filter", err)}
}
""".lstrip())

# -------------------------------------------------------------------------
# 4. The status filter. Every error a route returns comes through here, and
#    what it did was look at the value in hand: one comparison by identity
#    and one switch on the concrete type. Both stop working the moment a
#    caller adds context, which sections 3 and 7 are about to make routine.
#
#    `errors.As` walks the chain and stops at the first error that answers
#    for a status, so an error reported plain and the same error reported
#    under two layers of context come out as the same status. An error that
#    stands for nothing in particular is still a 500.
#
#    `goof.Newe` went away with it. It existed to normalise an arbitrary
#    error into a goof before the envelope took it; `types.NewHTTPError`
#    does that itself, and the log line now carries the error the route
#    actually returned rather than a copy of it.
# -------------------------------------------------------------------------
edit("api/server/handlers/handlers_errors.go",
     ('import (\n'
      '\t"encoding/json"\n'
      '\t"net/http"\n'
      '\n'
      '\tlog "github.com/Sirupsen/logrus"\n'
      '\t"github.com/akutz/goof"\n',
      'import (\n'
      '\t"encoding/json"\n'
      '\t"errors"\n'
      '\t"net/http"\n'
      '\n'
      '\tlog "github.com/Sirupsen/logrus"\n'),

     ('\tgerr := goof.Newe(err)\n'
      '\tctx.WithError(gerr).Error("error: api call failed")\n'
      '\n'
      '\thttpErr := goof.NewHTTPError(gerr, getStatus(err))\n',
      '\tctx.WithError(err).Error("error: api call failed")\n'
      '\n'
      '\thttpErr := types.NewHTTPError(err, getStatus(err))\n'),

     ('func getStatus(err error) int {\n'
      '\tif err == types.ErrMissingStorageService {\n'
      '\t\treturn http.StatusInternalServerError\n'
      '\t}\n'
      '\tswitch err.(type) {\n'
      '\tcase *types.ErrBadAdminToken,\n'
      '\t\t*types.ErrSecTokInvalid:\n'
      '\t\treturn http.StatusUnauthorized\n'
      '\tcase *types.ErrNotFound:\n'
      '\t\treturn http.StatusNotFound\n'
      '\tcase *types.ErrMissingInstanceID,\n'
      '\t\t*types.ErrMissingLocalDevices:\n'
      '\t\treturn http.StatusBadRequest\n'
      '\tdefault:\n'
      '\t\treturn http.StatusInternalServerError\n'
      '\t}\n'
      '}\n',
      '// getStatus returns the HTTP status an error is reported with. The'
      ' status\n'
      '// belongs to the error rather than to this function: the outermost'
      ' error in\n'
      '// the chain that stands for a status decides, so an error that'
      ' already\n'
      '// represents the outcome of an operation keeps its own status when'
      ' something\n'
      '// wraps it, and an error that was wrapped on the way up here is still'
      ' reported\n'
      '// as what it is. An error that stands for nothing in particular is an'
      ' internal\n'
      '// error.\n'
      'func getStatus(err error) int {\n'
      '\tvar statusErr types.HTTPStatusError\n'
      '\tif errors.As(err, &statusErr) {\n'
      '\t\treturn statusErr.HTTPStatus()\n'
      '\t}\n'
      '\treturn http.StatusInternalServerError\n'
      '}\n'))

# -------------------------------------------------------------------------
# 5. The other end of the wire. The client decodes an error response body
#    back into an envelope; the envelope is now this repository's, and the
#    fallback for a body that will not decode is too.
# -------------------------------------------------------------------------
edit("api/client/client_http.go",
     ('\t"net/http"\n'
      '\t"sync"\n'
      '\n'
      '\t"github.com/akutz/goof"\n'
      '\n'
      '\t"github.com/codedellemc/libstorage/api/context"\n',
      '\t"net/http"\n'
      '\t"sync"\n'
      '\n'
      '\t"github.com/codedellemc/libstorage/api/context"\n'),
     ("goof.DecodeHTTPError(res.Body)", "types.DecodeHTTPError(res.Body)"),
     ('goof.WithField("status", res.StatusCode, "http error")',
      'types.WithField("status", res.StatusCode, "http error")'))

# -------------------------------------------------------------------------
# 6. The sites that decided what an error was by looking at the value in
#    hand. Seven of the nine are here; the other two were the identity
#    comparison and the type switch in section 4. All of them answered
#    correctly for as long as nothing was wrapped around the error, and all
#    of them ask the chain now.
#
#    The assertion in the mock test is not one of them -- `goof.HTTPError`
#    is an interface, and asking whether an error answers some question is
#    not the construct being retired. It changes because the interface it
#    names is going away, and it becomes an `errors.As` because the envelope
#    that replaces it is a concrete type.
# -------------------------------------------------------------------------
edit("drivers/integration/linux/linux_utils.go",
     ('func isErrNotFound(err error) bool {\n'
      '\tswitch err.(type) {\n'
      '\tcase *types.ErrNotFound:\n'
      '\t\treturn true\n'
      '\tdefault:\n'
      '\t\treturn false\n'
      '\t}\n'
      '}\n',
      'func isErrNotFound(err error) bool {\n'
      '\tvar notFound *types.ErrNotFound\n'
      '\treturn errors.As(err, &notFound)\n'
      '}\n'))

edit("drivers/os/linux/linux.go",
     ("\tif err != nil && err != errUnknownFileSystem {\n",
      "\tif err != nil && !errors.Is(err, errUnknownFileSystem) {\n"))

edit("drivers/storage/libstorage/libstorage_client.go",
     ("\t\t\tif err == types.ErrNotImplemented {\n",
      "\t\t\tif errors.Is(err, types.ErrNotImplemented) {\n"))

edit("drivers/storage/libstorage/libstorage_client_xcli.go",
     ("\t\tif err.Error() == types.ErrNotImplemented.Error() {\n",
      "\t\tif errors.Is(err, types.ErrNotImplemented) {\n"))

edit("drivers/storage/libstorage/libstorage_utils.go",
     ('import (\n'
      '\t"strings"\n',
      'import (\n'
      '\t"errors"\n'
      '\t"strings"\n'),
     ("\t\t\tif err == errExecutorNotSupported ||\n"
      "\t\t\t\terr == types.ErrNotImplemented {\n",
      "\t\t\tif errors.Is(err, errExecutorNotSupported) ||\n"
      "\t\t\t\terrors.Is(err, types.ErrNotImplemented) {\n"))

edit("drivers/storage/libstorage/libstorage_driver_tls.go",
     ("\t\tif _, ok := err.(*types.ErrKnownHost); !ok {\n",
      "\t\tvar knownHostErr *types.ErrKnownHost\n"
      "\t\tif !errors.As(err, &knownHostErr) {\n"))

edit("drivers/storage/mock/tests/mock_test.go",
     ("\t\thttpErr := err.(goof.HTTPError)\n",
      "\t\tvar httpErr *types.HTTPError\n"
      "\t\tassert.True(t, errors.As(err, &httpErr))\n"))

# -------------------------------------------------------------------------
# 7. The four places that rendered an error into a message and kept only
#    the text. `%w` keeps the value as well, which is all these needed:
#    the message each one produces is unchanged, because that is what %w
#    formats to.
# -------------------------------------------------------------------------
sub("api/server/handlers/handlers_schema_validator.go",
    'read req error: %v", err)', 'read req error: %w", err)', 1)
sub("api/server/handlers/handlers_schema_validator.go",
    'validation error: %v", err)', 'validation error: %w", err)', 1)
sub("api/server/handlers/handlers_schema_validator.go",
    'unmarshal error: %v", err)', 'unmarshal error: %w", err)', 1)
sub("drivers/os/linux/linux_mount.go",
    'fmt.Errorf("Scanning \'%s\' failed: %s", text, err)',
    'fmt.Errorf("Scanning \'%s\' failed: %w", text, err)', 1)

# -------------------------------------------------------------------------
# 8. The remaining call sites, file by file. Nothing here is a decision:
#    a goof with no fields is an `errors.New`, and a goof with fields is the
#    same constructor under `types`. The counts are what make this safe to
#    do mechanically -- a file that does not contain exactly what is stated
#    below stops the script.
# -------------------------------------------------------------------------
GOOF = '\t"github.com/akutz/goof"\n'


def sweep(path, imports, *counts):
    """Fix a file's imports, then replace each (old, new, n) call site."""
    edit(path, *imports)
    for old, new, n in counts:
        sub(path, old, new, n)


sweep("api/context/context_keys.go",
      [('\t"sync"\n\n' + GOOF + '\n', '\t"sync"\n\n')],
      ("goof.WithField(", "types.WithField(", 1))

sweep("api/registry/registry.go",
      [(GOOF, "")],
      ("goof.WithField(", "types.WithField(", 4))

sweep("api/server/server_http.go",
      [('\t"crypto/tls"\n', '\t"crypto/tls"\n\t"errors"\n'), (GOOF, "")],
      ("goof.WithError(", "types.WithError(", 1),
      ("goof.WithField(", "types.WithField(", 1),
      ("goof.New(", "errors.New(", 1))

sweep("api/server/router/snapshot/snapshot_routes.go",
      [('import (\n\t"net/http"\n', 'import (\n\t"errors"\n\t"net/http"\n'),
       (GOOF, "")],
      ("goof.New(", "errors.New(", 1))

sweep("api/server/router/volume/volume_routes.go",
      [('import (\n\t"net/http"\n', 'import (\n\t"errors"\n\t"net/http"\n'),
       (GOOF, "")],
      ("goof.New(", "errors.New(", 1))

sweep("api/server/services/services.go",
      [(GOOF, "")],
      ("goof.WithFields(goof.Fields{",
       "types.WithFields(map[string]interface{}{", 1))

sweep("api/server/services/services_storage.go",
      [(GOOF, "")],
      ("goof.WithField(", "types.WithField(", 1))

sweep("api/server/services/services_task.go",
      [('\t"encoding/json"\n', '\t"encoding/json"\n\t"errors"\n'),
       ("\n" + GOOF, "")],
      ("goof.New(", "errors.New(", 1))

sweep("api/types/types_instanceid.go",
      [('\t"encoding/json"\n', '\t"encoding/json"\n\t"errors"\n'),
       ("\n\n" + GOOF + ")", "\n)")],
      ("goof.New(", "errors.New(", 3),
      ("goof.WithField(", "WithField(", 1))

sweep("api/types/types_localdevices.go",
      [("\n\n" + GOOF + ")", "\n)")],
      ("goof.WithField(", "WithField(", 1))

sweep("api/types/types_tx.go",
      [("\n\n" + GOOF + ")", "\n)")],
      ("goof.WithField(", "WithField(", 1))

sweep("api/types/types_uuid.go",
      [('\t"encoding/hex"\n', '\t"encoding/hex"\n\t"errors"\n'),
       ("\n\n" + GOOF + ")", "\n)")],
      ("goof.New(", "errors.New(", 1))

sweep("api/utils/filters/filters.go",
      [('\t"bytes"\n', '\t"bytes"\n\t"errors"\n'), (GOOF + "\n", "")],
      ("goof.New(", "errors.New(", 4),
      ("goof.WithField(", "types.WithField(", 1))

sweep("api/utils/utils_tls.go",
      [(GOOF, "")],
      ("goof.WithFieldE(", "types.WithFieldE(", 2),
      ("goof.WithFields(", "types.WithFields(", 1),
      ("goof.WithField(", "types.WithField(", 3),
      ("goof.WithError(", "types.WithError(", 2))

sweep("drivers/integration/linux/linux.go",
      [('import (\n', 'import (\n\t"errors"\n'), (GOOF, "")],
      ("goof.WithError(", "types.WithError(", 2),
      ("goof.New(", "errors.New(", 14))

sweep("drivers/integration/linux/linux_utils.go",
      [('import (\n\t"fmt"\n', 'import (\n\t"errors"\n\t"fmt"\n'), (GOOF, "")],
      ("goof.New(", "errors.New(", 2))

sweep("drivers/os/darwin/darwin.go",
      [('import (\n\t"runtime"\n', 'import (\n\t"errors"\n\t"runtime"\n'),
       (GOOF, "")],
      ("goof.New(", "errors.New(", 3))

sweep("drivers/os/linux/linux.go",
      [('\t"bytes"\n', '\t"bytes"\n\t"errors"\n'), (GOOF, "")],
      ("goof.WithFieldsE(goof.Fields{",
       "types.WithFieldsE(map[string]interface{}{", 1),
      ("goof.WithFieldE(", "types.WithFieldE(", 2),
      ("goof.WithField(", "types.WithField(", 1),
      ("goof.WithError(", "types.WithError(", 1),
      ("goof.New(", "errors.New(", 4))

sweep("drivers/storage/libstorage/libstorage_client.go",
      [(GOOF, "")],
      ("goof.WithError(", "types.WithError(", 2),
      ("goof.WithField(", "types.WithField(", 1))

sweep("drivers/storage/libstorage/libstorage_client_xcli.go",
      [('import (\n\t"strings"\n', 'import (\n\t"errors"\n\t"strings"\n'),
       (GOOF, "")],
      ("goof.New(", "errors.New(", 8))

sweep("drivers/storage/libstorage/libstorage_driver_funcs.go",
      [(GOOF, '\t"errors"\n')],
      ("goof.New(", "errors.New(", 17))

sweep("drivers/storage/mock/mock_driver.go",
      [('import (\n\t"fmt"\n', 'import (\n\t"errors"\n\t"fmt"\n'), (GOOF, "")],
      ("goof.WithFieldE(", "types.WithFieldE(", 2),
      ("goof.New(", "errors.New(", 2))

sweep("drivers/storage/mock/tests/mock_test.go",
      [('\t"encoding/json"\n', '\t"encoding/json"\n\t"errors"\n'),
       (GOOF, "")],
      ("goof.NewHTTPError(", "types.NewHTTPError(", 1),
      ("goof.WithFieldE(", "types.WithFieldE(", 2),
      ("goof.New(", "errors.New(", 1))

sweep("drivers/storage/vfs/executor/vfs_executor.go",
      [('\t"bufio"\n', '\t"bufio"\n\t"errors"\n'), (GOOF, "")],
      ("goof.New(", "errors.New(", 3))

sweep("drivers/storage/vfs/storage/vfs_storage.go",
      [(GOOF, "")],
      ("goof.WithFieldE(", "types.WithFieldE(", 2),
      ("goof.New(", "errors.New(", 3))
PY

# ---------------------------------------------------------------------------
# 9. Nothing first-party imports goof any more, so `tidy` demotes it to the
#    indirect requirement it has become -- gofig still reaches it, which is why
#    the requirement does not disappear altogether. Then the checks step 1
#    established, plus a cross-compile for the one platform whose OS driver
#    this milestone edited and this one cannot otherwise compile.
# ---------------------------------------------------------------------------
go mod tidy

unformatted=$(gofmt -l -s $(find . -name '*.go' -not -path './vendor/*'))
if [ -n "$unformatted" ]; then
    echo "not gofmt -s clean:" >&2
    echo "$unformatted" >&2
    exit 1
fi

go build ./...
go vet ./...
GOOS=darwin go build ./...
