// Observes the IssuanceChainStorage interface this release introduces at
// trillian/ctfe/storage, the backend-selection contract of its constructor,
// and the wire from that constructor into a set-up log instance.
//
// SCOPE-DB: no database server runs here. Every case below is decided entirely
// offline -- the two constructor outcomes that would need a live server (a
// well-formed mysql DSN) are deliberately not exercised, because the product's
// mysql store dials at construction and calls klog.Exitf when it cannot.
//
// The driver does define an in-memory implementation of the interface, but
// nothing below asserts anything about it: a driver-owned map can only answer
// with what the driver put in it, so storing a chain and reading it back would
// grade the map rather than the product. It is kept purely as a compile-time
// guard that the interface is implementable from outside its own package. The
// method set itself is asserted by reflection over the candidate's declaration,
// and the constructor's answer is asserted through SetUpInstance, which is the
// only place in the product that consumes an IssuanceChainStorage offline.
package main

import (
	"context"
	"encoding/pem"
	"errors"
	"reflect"
	"sort"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/certificate-transparency-go/trillian/ctfe/storage"
	"github.com/google/trillian/crypto/keys"
	"github.com/google/trillian/crypto/keys/der"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"
	"google.golang.org/protobuf/types/known/anypb"

	"ctverify/vhelp"
)

// memStorage is a compile-time guard only: this file asserts nothing about its
// contents. It exists so that the build fails if the candidate's interface
// cannot be satisfied by a type declared outside the storage package -- i.e.
// if it grew an unexported method or changed a signature.
type memStorage struct{ m map[string][]byte }

func (s *memStorage) FindByKey(_ context.Context, key []byte) ([]byte, error) {
	v, ok := s.m[string(key)]
	if !ok {
		return nil, errors.New("no such key")
	}
	return v, nil
}

func (s *memStorage) Add(_ context.Context, key []byte, chain []byte) error {
	s.m[string(key)] = append([]byte(nil), chain...)
	return nil
}

var _ storage.IssuanceChainStorage = (*memStorage)(nil)

// methodSet renders an interface type's methods as a single stable string.
// The slice is sorted here rather than relying on reflect's own ordering, so
// nothing about this comparison depends on how the runtime lays the type out.
func methodSet(it reflect.Type) string {
	parts := make([]string, 0, it.NumMethod())
	for i := 0; i < it.NumMethod(); i++ {
		m := it.Method(i)
		ft := m.Type
		in := make([]string, 0, ft.NumIn())
		for j := 0; j < ft.NumIn(); j++ {
			in = append(in, ft.In(j).String())
		}
		out := make([]string, 0, ft.NumOut())
		for j := 0; j < ft.NumOut(); j++ {
			out = append(out, ft.Out(j).String())
		}
		sig := m.Name + "(" + strings.Join(in, ", ") + ")"
		switch len(out) {
		case 0:
		case 1:
			sig += " " + out[0]
		default:
			sig += " (" + strings.Join(out, ", ") + ")"
		}
		parts = append(parts, sig)
	}
	sort.Strings(parts)
	return strings.Join(parts, "; ")
}

// logConfig returns a configuration that is valid at this release and differs
// from the next only in which issuance-chain storage backend it names.
func logConfig(t *vhelp.T, backend configpb.LogConfig_IssuanceChainStorageBackend) *configpb.LogConfig {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	if blk == nil {
		t.Fatalf("pinned log key fixture is not PEM")
	}
	priv, err := anypb.New(&keyspb.PrivateKey{Der: blk.Bytes})
	if err != nil {
		t.Fatalf("wrap private key: %v", err)
	}
	return &configpb.LogConfig{
		LogId:                                1001,
		Prefix:                               "bench",
		RootsPemFile:                         []string{vhelp.Fixture("minted", "roots-bench.pem")},
		PrivateKey:                           priv,
		ExtraDataIssuanceChainStorageBackend: backend,
	}
}

// setUp validates a config naming the given backend and sets a log up from it.
// No connection string is ever supplied, so no arm of the storage factory can
// dial anything.
func setUp(t *vhelp.T, backend configpb.LogConfig_IssuanceChainStorageBackend) (*ctfe.Instance, error) {
	v, err := ctfe.ValidateLogConfig(logConfig(t, backend))
	if err != nil {
		return nil, err
	}
	return ctfe.SetUpInstance(context.Background(), ctfe.InstanceOptions{
		Validated:     v,
		Deadline:      time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    &ctfe.DefaultRequestLog{},
	})
}

func main() {
	t := vhelp.New()
	ctx := context.Background()
	keys.RegisterHandler(&keyspb.PrivateKey{}, der.FromProto)

	// 1. The interface, read off the candidate's own declaration. Exactly two
	//    methods, both exported -- NumMethod counts unexported ones too for an
	//    interface type, so a third or a hidden one fails here -- with the
	//    signatures CONFIG.md section 2.6 fixes. Nothing the driver declares
	//    takes part in this comparison.
	iface := reflect.TypeOf((*storage.IssuanceChainStorage)(nil)).Elem()
	t.Equal("iface-method-set-is-exactly-findbykey-and-add", methodSet(iface),
		"Add(context.Context, []uint8, []uint8) error; "+
			"FindByKey(context.Context, []uint8) ([]uint8, error)")

	// 2. The constructor's answer is what the personality is actually built
	//    with, so a store it refuses to build must stop a log from setting up
	//    and the nil store the Trillian-gRPC arm returns must not be mistaken
	//    for a refusal. An out-of-enum backend is the wedge that isolates the
	//    factory: ValidateLogConfig carries such a value through untouched, so
	//    by the time setUpLogInfo runs, NewIssuanceChainStorage is the only
	//    thing left that can object -- and it is reached before the cache
	//    factory, which this configuration gives nothing to object to. The
	//    Trillian-gRPC half is the control: without it a personality that
	//    failed to set up for any reason at all would score the refusal.
	//    Both outcomes are the product's; neither is decided here.
	okInst, okErr := setUp(t, configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC)
	badInst, badErr := setUp(t, configpb.LogConfig_IssuanceChainStorageBackend(7))
	t.Check("storage-factory-decides-whether-the-log-sets-up",
		okInst != nil && okErr == nil && badInst == nil && badErr != nil,
		"TRILLIAN_GRPC set up to (%v, %v), want an instance; backend 7 set up to (%v, %v), want (<nil>, <error>)",
		okInst, okErr, badInst, badErr)

	// The constructor's backend selection, in isolation.
	st, err := storage.NewIssuanceChainStorage(ctx, configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC, "")
	t.Check("new-trillian-grpc-is-nil-nil", st == nil && err == nil,
		"NewIssuanceChainStorage(TRILLIAN_GRPC, \"\") = (%v, %v), want (<nil>, <nil>)", st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC,
		"mysql://ctfe:letmein@tcp(127.0.0.1:3306)/ctfe")
	t.Check("new-trillian-grpc-ignores-dsn", st == nil && err == nil,
		"NewIssuanceChainStorage(TRILLIAN_GRPC, <mysql dsn>) = (%v, %v), want (<nil>, <nil>)", st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE,
		"postgres://ctfe:letmein@127.0.0.1:5432/ctfe")
	t.Check("new-ctfe-rejects-non-mysql-dsn", st == nil && err != nil,
		"NewIssuanceChainStorage(CTFE, <postgres dsn>) = (%v, %v), want (<nil>, <error>)", st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE, "")
	t.Check("new-ctfe-rejects-empty-dsn", st == nil && err != nil,
		"NewIssuanceChainStorage(CTFE, \"\") = (%v, %v), want (<nil>, <error>)", st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, configpb.LogConfig_IssuanceChainStorageBackend(7), "")
	t.Check("new-unknown-backend-rejected", st == nil && err != nil,
		"NewIssuanceChainStorage(backend 7, \"\") = (%v, %v), want (<nil>, <error>)", st, err)

	t.Done()
}
