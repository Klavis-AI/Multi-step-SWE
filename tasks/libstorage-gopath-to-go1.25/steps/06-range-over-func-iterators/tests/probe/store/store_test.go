// Package store asks what the repository's key/value store returns, and
// nothing about how it is written.
//
// `types.Store` is a bag of `interface{}` behind twenty-two accessors, and the
// accessors come in two families that look identical and are not. Six of them
// type-assert and return the zero value when the assertion fails. Eight of them
// *convert*: they reach for `strconv` and `fmt` when the stored value is not
// already the type being asked for, because this store is filled from
// configuration and from JSON and almost nothing in it arrives as the type its
// reader wants.
//
// The obvious way to collapse fourteen near-identical methods onto one generic
// helper is
//
//	func Get[T any](s types.Store, k string) T { v, _ := s.Get(k).(T); return v }
//
// which compiles, reads well, and silently deletes every conversion in the
// second family. Every test here is a value this store returns today. None of
// them names a method's implementation, a helper or a type parameter: they
// store something and read it back, which is all any caller does.
//
// These are controls. They pass on the tree this step starts from and they have
// to keep passing, because collapsing the first family is the milestone and
// changing what the second family answers is not.
package store

import (
	"testing"

	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
)

func newStore(t *testing.T, data map[string]interface{}) types.Store {
	t.Helper()
	return utils.NewStoreWithData(data)
}

// The coercing family: a value stored as one type, read back as another.

func TestGetStringRendersNonStrings(t *testing.T) {
	s := newStore(t, map[string]interface{}{
		"i": 42, "b": true, "i64": int64(9), "f": 1.5,
	})
	for _, c := range []struct{ k, want string }{
		{"i", "42"}, {"b", "true"}, {"i64", "9"}, {"f", "1.5"},
	} {
		if got := s.GetString(c.k); got != c.want {
			t.Errorf("GetString(%q) = %q, want %q", c.k, got, c.want)
		}
	}
}

func TestGetBoolParsesStoredStrings(t *testing.T) {
	s := newStore(t, map[string]interface{}{
		"t": "true", "one": "1", "f": "false", "zero": "0", "junk": "nope",
	})
	for _, c := range []struct {
		k    string
		want bool
	}{{"t", true}, {"one", true}, {"f", false}, {"zero", false},
		{"junk", false}} {
		if got := s.GetBool(c.k); got != c.want {
			t.Errorf("GetBool(%q) = %v, want %v", c.k, got, c.want)
		}
	}
}

func TestGetIntAndInt64ParseStoredStrings(t *testing.T) {
	s := newStore(t, map[string]interface{}{"n": "42", "junk": "nope"})
	if got := s.GetInt("n"); got != 42 {
		t.Errorf("GetInt(\"n\") = %d, want 42", got)
	}
	if got := s.GetInt64("n"); got != 42 {
		t.Errorf("GetInt64(\"n\") = %d, want 42", got)
	}
	if got := s.GetInt("junk"); got != 0 {
		t.Errorf("GetInt(\"junk\") = %d, want 0", got)
	}
	if got := s.GetInt64("junk"); got != 0 {
		t.Errorf("GetInt64(\"junk\") = %d, want 0", got)
	}
}

// The pointer variants, which do not agree with each other. A reader who
// assumes they do -- or a rewrite that makes them agree -- changes what two of
// these three answer.

func TestPointerGettersDisagreeOnAParseFailure(t *testing.T) {
	s := newStore(t, map[string]interface{}{"junk": "nope"})
	if p := s.GetIntPtr("junk"); p != nil {
		t.Errorf("GetIntPtr on an unparseable value = %v, want nil", *p)
	}
	if p := s.GetInt64Ptr("junk"); p != nil {
		t.Errorf("GetInt64Ptr on an unparseable value = %v, want nil", *p)
	}
	p := s.GetBoolPtr("junk")
	if p == nil {
		t.Fatal("GetBoolPtr on an unparseable value = nil, want a pointer")
	}
	if *p {
		t.Errorf("GetBoolPtr on an unparseable value points at true")
	}
}

func TestPointerGettersReadThroughAStoredPointer(t *testing.T) {
	sv, bv, iv := "true", true, 42
	s := newStore(t, map[string]interface{}{"s": &sv, "b": &bv, "i": &iv})
	if p := s.GetBoolPtr("s"); p == nil || !*p {
		t.Errorf("GetBoolPtr through a *string(\"true\") = %v, want true", p)
	}
	if p := s.GetBoolPtr("b"); p != &bv {
		t.Errorf("GetBoolPtr on a stored *bool did not return it")
	}
	if p := s.GetIntPtr("i"); p != &iv {
		t.Errorf("GetIntPtr on a stored *int did not return it")
	}
}

func TestGetStringPtrShapes(t *testing.T) {
	sv := "stored"
	s := newStore(t, map[string]interface{}{"p": &sv, "v": "plain"})
	if p := s.GetStringPtr("p"); p != &sv {
		t.Errorf("GetStringPtr on a stored *string did not return it")
	}
	p := s.GetStringPtr("v")
	if p == nil || *p != "plain" {
		t.Fatalf("GetStringPtr on a stored string = %v, want a pointer to it", p)
	}
	if p := s.GetStringPtr("absent"); p != nil {
		t.Errorf("GetStringPtr on a missing key = %v, want nil", *p)
	}
}

// The asserting family: the zero value when the stored type is not the type
// asked for, and never a panic.

func TestAssertingGettersReturnNilOnTheWrongType(t *testing.T) {
	s := newStore(t, map[string]interface{}{"i": 42, "s": "text"})
	if v := s.GetStore("i"); v != nil {
		t.Errorf("GetStore on a stored int = %v, want nil", v)
	}
	if v := s.GetStringSlice("s"); v != nil {
		t.Errorf("GetStringSlice on a stored string = %v, want nil", v)
	}
	if v := s.GetIntSlice("s"); v != nil {
		t.Errorf("GetIntSlice on a stored string = %v, want nil", v)
	}
	if v := s.GetBoolSlice("s"); v != nil {
		t.Errorf("GetBoolSlice on a stored string = %v, want nil", v)
	}
	if v := s.GetMap("s"); v != nil {
		t.Errorf("GetMap on a stored string = %v, want nil", v)
	}
	if v := s.GetInstanceID("i"); v != nil {
		t.Errorf("GetInstanceID on a stored int = %v, want nil", v)
	}
}

func TestAssertingGettersReturnWhatWasStored(t *testing.T) {
	inner := utils.NewStore()
	iid := &types.InstanceID{ID: "iid-1", Driver: "vfs"}
	s := newStore(t, map[string]interface{}{
		"store": inner,
		"iid":   iid,
		"ss":    []string{"a", "b"},
		"is":    []int{1, 2},
		"bs":    []bool{true},
		"m":     map[string]interface{}{"k": "v"},
	})
	if got := s.GetStore("store"); got != inner {
		t.Errorf("GetStore did not return the stored Store")
	}
	if got := s.GetInstanceID("iid"); got != iid {
		t.Errorf("GetInstanceID did not return the stored InstanceID")
	}
	if got := s.GetStringSlice("ss"); len(got) != 2 || got[0] != "a" {
		t.Errorf("GetStringSlice = %v", got)
	}
	if got := s.GetIntSlice("is"); len(got) != 2 || got[1] != 2 {
		t.Errorf("GetIntSlice = %v", got)
	}
	if got := s.GetBoolSlice("bs"); len(got) != 1 || !got[0] {
		t.Errorf("GetBoolSlice = %v", got)
	}
	if got := s.GetMap("m"); got == nil || got["k"] != "v" {
		t.Errorf("GetMap = %v", got)
	}
}

// The store's own defining properties, which no rewrite of the accessors may
// disturb.

func TestKeysAreCaseInsensitive(t *testing.T) {
	s := utils.NewStore()
	s.Set("MixedCase", "v")
	for _, k := range []string{"MixedCase", "mixedcase", "MIXEDCASE"} {
		if got := s.GetString(k); got != "v" {
			t.Errorf("GetString(%q) = %q, want \"v\"", k, got)
		}
		if !s.IsSet(k) {
			t.Errorf("IsSet(%q) = false", k)
		}
	}
}

func TestMissingKeysReturnZeroValues(t *testing.T) {
	s := utils.NewStore()
	if got := s.GetString("absent"); got != "" {
		t.Errorf("GetString on a missing key = %q", got)
	}
	if got := s.GetInt("absent"); got != 0 {
		t.Errorf("GetInt on a missing key = %d", got)
	}
	if got := s.GetInt64("absent"); got != 0 {
		t.Errorf("GetInt64 on a missing key = %d", got)
	}
	if got := s.GetBool("absent"); got {
		t.Errorf("GetBool on a missing key = true")
	}
	if got := s.Get("absent"); got != nil {
		t.Errorf("Get on a missing key = %v", got)
	}
	if s.IsSet("absent") {
		t.Errorf("IsSet on a missing key = true")
	}
}
