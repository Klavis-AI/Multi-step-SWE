// Package errwrap asks one question of the repository's own error API: when a
// constructor is handed the error that caused the one it is building, is that
// cause still reachable from the outside?
//
// Two of the constructors in api/utils take a cause, and several call sites
// hand them one. Today the cause is stored in a field and nothing can walk to
// it: the value that comes back answers `Error()` and marshals to JSON, and a
// caller asking "was this ultimately a not-found?" has no way to find out. The
// text is kept and the value is lost, which is the same defect as rendering an
// error into a message with %v -- only written from the other end.
//
// Everything here goes through `errors.Is` and `errors.As`, because those are
// the two questions a caller has after a cause becomes reachable, and because
// they are answered by walking the chain rather than by looking at the value in
// hand. Nothing here names a field, a helper or a spelling.
//
// The controls are the other half. An error chain is not the only thing this
// API is: the same values are what the server puts on the wire and what a task
// record carries, so their text and their JSON have to come out exactly as
// they do today. A tree that made causes reachable by replacing the error API
// wholesale would pass the two graded tests and fail these three.
package errwrap

import (
	"encoding/json"
	"errors"
	"fmt"
	"reflect"
	"testing"

	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
)

// causes are the three kinds of cause a call site has to hand a constructor:
// an error from outside this repository, one of the repository's own
// sentinels, and one of its constructed errors.
var causes = []struct {
	name  string
	cause error
}{
	{"a foreign error", errors.New("the volume service went away")},
	{"a sentinel of this repository", types.ErrNotImplemented},
	{"a constructed error", utils.NewNotFoundError("vfs-000")},
}

// reachable is the assertion both of the tests below make: the cause that was
// handed to a constructor is still reachable from the error it returned.
func reachable(t *testing.T, name string, build func(error) error) {
	t.Helper()
	for _, c := range causes {
		err := build(c.cause)
		if !errors.Is(err, c.cause) {
			t.Errorf("%s given %s: the cause is not reachable from the "+
				"error that was built around it\n  error: %v\n  cause: %v",
				name, c.name, err, c.cause)
		}
	}
}

// TestBadFilterKeepsItsCause is the requirement, asked of the constructor a
// malformed filter goes through. The parse error it is given is the only thing
// that says what was wrong with the filter.
func TestBadFilterKeepsItsCause(t *testing.T) {
	reachable(t, "bad filter", func(cause error) error {
		return utils.NewBadFilterErr("(name", cause)
	})
}

// TestBatchProcessKeepsItsCause is the same requirement asked of the other
// constructor that takes one. A batch error's whole purpose is to say what
// stopped it, and today it says so only in the text.
func TestBatchProcessKeepsItsCause(t *testing.T) {
	reachable(t, "batch process", func(cause error) error {
		return utils.NewBatchProcessErr([]string{"vfs-000"}, cause)
	})
}

// TestCauseKeepsItsTypeThroughAWrap is the same requirement asked by type
// rather than by identity, and asked through a second layer, because a chain
// that only survives one link is not a chain. The outer layer is an ordinary
// fmt.Errorf, which is what a call site that has no constructor of its own
// writes.
func TestCauseKeepsItsTypeThroughAWrap(t *testing.T) {
	inner := utils.NewBatchProcessErr(
		[]string{"vfs-000"}, utils.NewNotFoundError("vfs-001"))
	outer := fmt.Errorf("attaching the volume: %w", inner)

	var notFound *types.ErrNotFound
	if !errors.As(outer, &notFound) {
		t.Fatalf("the not-found at the root of the chain is not reachable "+
			"through two layers: %v", outer)
	}
	if got := notFound.Error(); got != "resource not found" {
		t.Errorf("the error reached through the chain is not the one that "+
			"was put there: %q", got)
	}

	var batch *types.ErrBatchProcess
	if !errors.As(outer, &batch) {
		t.Errorf("the error the outer layer was wrapped around is not "+
			"reachable: %v", outer)
	}
	if !errors.Is(outer, inner) {
		t.Errorf("the error the outer layer was wrapped around is not "+
			"reachable by identity: %v", outer)
	}
}

// TestErrorTextIsUnchanged is a control. Every message below is what the
// server puts in the `message` field of an error response, so each one is a
// value this repository's callers already depend on.
//
// The expectations are pins, not choices: each is the string the tree this
// step starts from already returns from that error's `Error()`, read off that
// tree rather than picked, and several of them are the same text the recorded
// error responses carry in their `message` field. What makes them trustworthy
// is that the whole package passes against that tree -- these controls are
// graded in the regression family, where the pre-step tree has to score 1.0
// for the step to calibrate, so a value typed wrong here fails the calibration
// rather than sitting unnoticed. They cannot be generated at run time: this
// package is compiled inside the candidate's tree, which cannot read the
// verifier's data directory.
func TestErrorTextIsUnchanged(t *testing.T) {
	cause := errors.New("the volume service went away")

	cases := []struct {
		err  error
		want string
	}{
		{types.ErrNotImplemented, "not implemented"},
		{types.ErrTimedOut, "timed out"},
		{types.ErrMissingStorageService, "missing storage service"},
		{utils.NewNotFoundError("vfs-000"), "resource not found"},
		{utils.NewBadAdminTokenError("abc"), "invalid admin token"},
		{utils.NewMissingInstanceIDError("vfs"), "missing instance ID"},
		{utils.NewMissingLocalDevicesError("vfs"), "missing local devices"},
		{utils.NewStoreKeyErr("volumeID"), "missing store key"},
		{utils.NewDriverTypeErr("vfs", "mock"), "invalid driver type"},
		{utils.NewUnsupportedForClientTypeError(
			types.IntegrationClient, "volumeAttach"),
			"unsupported op for client type"},
		{utils.NewBatchProcessErr([]string{"vfs-000"}, cause),
			"batch processing error"},
		{utils.NewBadFilterErr("(name", cause), "bad filter"},
		{&types.ErrSecTokInvalid{}, "invalid security token"},
		{&types.ErrKnownHost{HostName: "h"}, "known host verification failed"},
		{&types.ErrKnownHostConflict{HostName: "h"},
			"known host signature has changed"},
	}

	for _, tc := range cases {
		if got := tc.err.Error(); got != tc.want {
			t.Errorf("Error() = %q, want %q", got, tc.want)
		}
	}
}

// TestErrorJSONIsUnchanged is a control, and the tighter of the two: these
// values are marshalled directly -- a task record carries its failure in an
// `error` field and encoding/json renders whatever is in it -- so their JSON
// is as much a part of this API as their text. The rule the cases below encode
// is the one the recorded responses show: an error that carries fields renders
// as those fields plus its message under `msg`. Errors that carry nothing but
// a message are deliberately absent: how such an error renders is not
// something any recorded response depends on, and pinning it here would be
// pinning a choice rather than a behavior.
//
// The expectations themselves are pins: each is the JSON that error already
// marshals to on the tree this step starts from, read off that tree rather
// than composed here. Some of these shapes are also visible in the recorded
// error responses -- the not-found's is the one the wire envelope and the
// failed task record both carry -- but the recordings are not the source of
// all six, the pre-step tree is. What makes them trustworthy is that the whole
// package passes against that tree: this control is graded in the regression
// family, where the pre-step tree has to score 1.0 for the step to calibrate,
// so a value typed wrong here fails the calibration rather than sitting
// unnoticed. They cannot be generated at run time either, because this package
// is compiled inside the candidate's tree and cannot read the verifier's data
// directory.
func TestErrorJSONIsUnchanged(t *testing.T) {
	cases := []struct {
		err  error
		want string
	}{
		{utils.NewNotFoundError("vfs-000"),
			`{"msg":"resource not found","resourceID":"vfs-000"}`},
		{utils.NewMissingInstanceIDError("vfs"),
			`{"msg":"missing instance ID","service":"vfs"}`},
		{utils.NewMissingLocalDevicesError("vfs"),
			`{"msg":"missing local devices","service":"vfs"}`},
		{utils.NewStoreKeyErr("volumeID"),
			`{"msg":"missing store key","storeKey":"volumeID"}`},
		{utils.NewBadAdminTokenError("abc"),
			`{"msg":"invalid admin token","token":"abc"}`},
		{utils.NewDriverTypeErr("vfs", "mock"),
			`{"msg":"invalid driver type","expectedType":"vfs",` +
				`"actualType":"mock"}`},
	}

	for _, tc := range cases {
		buf, err := json.Marshal(tc.err)
		if err != nil {
			t.Errorf("marshalling %v: %v", tc.err, err)
			continue
		}
		var got, want interface{}
		if err := json.Unmarshal(buf, &got); err != nil {
			t.Errorf("%v marshalled to something that is not JSON: %s",
				tc.err, buf)
			continue
		}
		if err := json.Unmarshal([]byte(tc.want), &want); err != nil {
			t.Fatalf("the expectation is not JSON: %s", tc.want)
		}
		if !reflect.DeepEqual(got, want) {
			t.Errorf("json.Marshal(%v) = %s, want %s", tc.err, buf, tc.want)
		}
	}
}

// TestErrorsAreStillClassifiable is a control on the other direction: the
// errors this API hands out are told apart by what they are, and they are told
// apart in the middleware that decides an HTTP status, in a driver deciding
// whether to retry, and in the repository's own tests. A migration that made
// causes reachable by collapsing the error types into one value would lose
// that, and it would lose it silently.
func TestErrorsAreStillClassifiable(t *testing.T) {
	var (
		notFound   *types.ErrNotFound
		instanceID *types.ErrMissingInstanceID
		localDevs  *types.ErrMissingLocalDevices
		adminToken *types.ErrBadAdminToken
		storeKey   *types.ErrStoreKey
		driverType *types.ErrDriverTypeErr
		badFilter  *types.ErrBadFilter
		batch      *types.ErrBatchProcess
		unsupp     *types.ErrUnsupportedForClientType
		secTok     *types.ErrSecTokInvalid
		knownHost  *types.ErrKnownHost
		hostConf   *types.ErrKnownHostConflict
	)
	cause := errors.New("the volume service went away")

	cases := []struct {
		name   string
		err    error
		target interface{}
	}{
		{"not found", utils.NewNotFoundError("vfs-000"), &notFound},
		{"missing instance ID",
			utils.NewMissingInstanceIDError("vfs"), &instanceID},
		{"missing local devices",
			utils.NewMissingLocalDevicesError("vfs"), &localDevs},
		{"bad admin token", utils.NewBadAdminTokenError("abc"), &adminToken},
		{"store key", utils.NewStoreKeyErr("volumeID"), &storeKey},
		{"driver type", utils.NewDriverTypeErr("vfs", "mock"), &driverType},
		{"bad filter", utils.NewBadFilterErr("(name", cause), &badFilter},
		{"batch process",
			utils.NewBatchProcessErr([]string{"vfs-000"}, cause), &batch},
		{"unsupported for client type",
			utils.NewUnsupportedForClientTypeError(
				types.IntegrationClient, "volumeAttach"), &unsupp},
		{"invalid security token", &types.ErrSecTokInvalid{}, &secTok},
		{"known host", &types.ErrKnownHost{HostName: "h"}, &knownHost},
		{"known host conflict",
			&types.ErrKnownHostConflict{HostName: "h"}, &hostConf},
	}

	for _, tc := range cases {
		if !errors.As(tc.err, tc.target) {
			t.Errorf("%s: the error is no longer recognisable as what it "+
				"is: %v", tc.name, tc.err)
		}
		// And it is not recognisable as something it is not.
		if tc.name != "not found" && errors.As(tc.err, &notFound) {
			t.Errorf("%s: recognised as a not-found: %v", tc.name, tc.err)
		}
	}
}
