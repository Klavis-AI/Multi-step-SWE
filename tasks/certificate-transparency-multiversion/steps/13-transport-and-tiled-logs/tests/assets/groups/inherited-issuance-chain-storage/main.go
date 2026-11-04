// INHERITED. The issuance-chain storage surface that step 12 introduced --
// the storage.IssuanceChainStorage interface, its backend-selection
// constructor, the cache factory with its LRU and no-op implementations, and
// the parts of ValidateLogConfig that police ctfe_storage_connection_string --
// must all keep behaving exactly as they did once this release adds a
// PostgreSQL implementation alongside them. Everything graded here is
// behaviour v1.2.0 already had, so this group is expected to pass against the
// previous tree as well.
//
// SCOPE-DB: no database server runs. Every verdict is decided by parsing and
// by in-memory data structures; no DSN named here is ever dialled, and the
// two arms that would need a live server (a well-formed mysql or postgres
// connection reaching a database) are deliberately not exercised.
package main

import (
	"bytes"
	"context"
	"encoding/pem"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/cache"
	"github.com/google/certificate-transparency-go/trillian/ctfe/cache/lru"
	"github.com/google/certificate-transparency-go/trillian/ctfe/cache/noop"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/certificate-transparency-go/trillian/ctfe/storage"
	"github.com/google/trillian/crypto/keyspb"
	"google.golang.org/protobuf/types/known/anypb"

	"ctverify/vhelp"
)

const (
	trillianGRPC = configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC
	ctfeBackend  = configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE

	mysqlDSN  = "mysql://ctfe:letmein@tcp(127.0.0.1:3306)/ctfe"
	sqliteDSN = "sqlite://file:/tmp/ctfe.db"
)

// memStorage is the driver's own implementation of the release's storage
// interface: it compiles only if the method set is exactly FindByKey/Add with
// these signatures.
type memStorage struct{ m map[string][]byte }

func (s *memStorage) FindByKey(_ context.Context, key []byte) ([]byte, error) {
	v, ok := s.m[string(key)]
	if !ok {
		return nil, errNotFound
	}
	return v, nil
}

func (s *memStorage) Add(_ context.Context, key []byte, chain []byte) error {
	s.m[string(key)] = append([]byte(nil), chain...)
	return nil
}

type notFound struct{}

func (notFound) Error() string { return "no such key" }

var errNotFound = notFound{}

// Pinned key/value pairs. No clock, no randomness, no network.
var (
	keyA   = []byte{0xa0, 0xa1, 0xa2, 0xa3}
	keyB   = []byte{0xb0, 0xb1, 0xb2, 0xb3}
	keyC   = []byte{0xc0, 0xc1, 0xc2, 0xc3}
	chainA = []byte{0x00, 0x00, 0x03, 0x01, 0x02, 0x03}
	chainB = []byte{0x00, 0x00, 0x02, 0x04, 0x05}
	chainC = []byte{0x00, 0x00, 0x01, 0x06}
)

func baseConfig(t *vhelp.T) *configpb.LogConfig {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	if blk == nil {
		t.Fatalf("pinned log key fixture is not PEM")
	}
	priv, err := anypb.New(&keyspb.PrivateKey{Der: blk.Bytes})
	if err != nil {
		t.Fatalf("wrap private key: %v", err)
	}
	return &configpb.LogConfig{LogId: 1001, Prefix: "bench", PrivateKey: priv}
}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	// --- the interface itself -------------------------------------------
	var s storage.IssuanceChainStorage = &memStorage{m: map[string][]byte{}}
	if err := s.Add(ctx, keyA, chainA); err != nil {
		t.Fatalf("Add through the interface failed: %v", err)
	}
	got, err := s.FindByKey(ctx, keyA)
	t.Check("interface-round-trips-through-a-driver-implementation",
		err == nil && bytes.Equal(got, chainA),
		"FindByKey after Add = (%x, %v), want (%x, <nil>)", got, err, chainA)

	got, err = s.FindByKey(ctx, keyB)
	t.Check("interface-find-absent-errors", got == nil && err != nil,
		"FindByKey on an absent key = (%x, %v), want (<nil>, <error>)", got, err)

	// --- backend selection ----------------------------------------------
	st, err := storage.NewIssuanceChainStorage(ctx, trillianGRPC, "")
	t.Check("trillian-grpc-backend-uses-no-storage", st == nil && err == nil,
		"NewIssuanceChainStorage(TRILLIAN_GRPC, \"\") = (%v, %v), want (<nil>, <nil>)", st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, trillianGRPC, mysqlDSN)
	t.Check("trillian-grpc-backend-ignores-the-connection-string", st == nil && err == nil,
		"NewIssuanceChainStorage(TRILLIAN_GRPC, <mysql dsn>) = (%v, %v), want (<nil>, <nil>)", st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, ctfeBackend, sqliteDSN)
	t.Check("ctfe-backend-rejects-an-unsupported-driver", st == nil && err != nil,
		"NewIssuanceChainStorage(CTFE, <sqlite dsn>) = (%v, %v), want (<nil>, <error>)", st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, ctfeBackend, "")
	t.Check("ctfe-backend-rejects-an-empty-connection-string", st == nil && err != nil,
		"NewIssuanceChainStorage(CTFE, \"\") = (%v, %v), want (<nil>, <error>)", st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, configpb.LogConfig_IssuanceChainStorageBackend(9), "")
	t.Check("unknown-storage-backend-is-rejected", st == nil && err != nil,
		"NewIssuanceChainStorage(backend 9, \"\") = (%v, %v), want (<nil>, <error>)", st, err)

	// --- the cache ------------------------------------------------------
	c, err := cache.NewIssuanceChainCache(ctx, cache.LRU, cache.Option{Size: 2, TTL: time.Hour})
	if err != nil || c == nil {
		t.Fatalf("NewIssuanceChainCache(lru) = (%v, %v)", c, err)
	}
	if err := c.Set(ctx, keyA, chainA); err != nil {
		t.Fatalf("lru Set failed: %v", err)
	}
	got, err = c.Get(ctx, keyA)
	t.Check("lru-cache-round-trips", err == nil && bytes.Equal(got, chainA),
		"lru Get after Set = (%x, %v), want (%x, <nil>)", got, err, chainA)

	// Capacity 2: A is already in, add B then C and A must be gone while the
	// two most recent survive.
	if err := c.Set(ctx, keyB, chainB); err != nil {
		t.Fatalf("lru Set failed: %v", err)
	}
	if err := c.Set(ctx, keyC, chainC); err != nil {
		t.Fatalf("lru Set failed: %v", err)
	}
	gotA, errA := c.Get(ctx, keyA)
	gotB, errB := c.Get(ctx, keyB)
	gotC, errC := c.Get(ctx, keyC)
	t.Check("lru-cache-evicts-the-least-recently-used-entry",
		gotA == nil && errA == nil && errB == nil && errC == nil &&
			bytes.Equal(gotB, chainB) && bytes.Equal(gotC, chainC),
		"after filling a size-2 cache: A=(%x,%v) B=(%x,%v) C=(%x,%v), want A absent and B,C present",
		gotA, errA, gotB, errB, gotC, errC)

	n, err := cache.NewIssuanceChainCache(ctx, cache.NOOP, cache.Option{})
	if err != nil || n == nil {
		t.Fatalf("NewIssuanceChainCache(noop) = (%v, %v)", n, err)
	}
	if _, isNoop := n.(*noop.IssuanceChainCache); !isNoop {
		t.Fatalf("NewIssuanceChainCache(noop) returned %T, want *noop.IssuanceChainCache", n)
	}
	setErr := n.Set(ctx, keyA, chainA)
	got, err = n.Get(ctx, keyA)
	t.Check("noop-cache-stores-nothing",
		setErr == nil && got == nil && err == nil,
		"noop Set=%v then Get=(%x, %v), want (<nil>) then (<nil>, <nil>)", setErr, got, err)

	// An empty type is the no-op cache; an unrecognised one is an error.
	d, err := cache.NewIssuanceChainCache(ctx, cache.Unknown, cache.Option{})
	_, dIsNoop := d.(*noop.IssuanceChainCache)
	t.Check("empty-cache-type-defaults-to-noop", err == nil && dIsNoop,
		"NewIssuanceChainCache(\"\") = (%T, %v), want (*noop.IssuanceChainCache, <nil>)", d, err)

	bad, err := cache.NewIssuanceChainCache(ctx, cache.Type("LRU"), cache.Option{Size: 1})
	t.Check("cache-factory-rejects-an-unknown-type", bad == nil && err != nil,
		"NewIssuanceChainCache(\"LRU\") = (%v, %v), want (<nil>, <error>)", bad, err)

	neg, err := cache.NewIssuanceChainCache(ctx, cache.LRU, cache.Option{Size: -1})
	t.Check("lru-cache-rejects-a-negative-size", neg == nil && err != nil,
		"NewIssuanceChainCache(lru, size -1) = (%v, %v), want (<nil>, <error>)", neg, err)

	// The concrete LRU constructor keeps its own option type.
	direct := lru.NewIssuanceChainCache(lru.CacheOption{Size: 1, TTL: time.Hour})
	if direct == nil {
		t.Fatalf("lru.NewIssuanceChainCache returned nil")
	}
	if err := direct.Set(ctx, keyB, chainB); err != nil {
		t.Fatalf("direct lru Set failed: %v", err)
	}
	got, err = direct.Get(ctx, keyB)
	t.Check("lru-constructor-takes-its-own-option-type",
		err == nil && bytes.Equal(got, chainB),
		"lru.NewIssuanceChainCache round trip = (%x, %v), want (%x, <nil>)", got, err, chainB)

	// --- configuration --------------------------------------------------
	cfg := baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = ctfeBackend
	cfg.CtfeStorageConnectionString = mysqlDSN
	v, err := ctfe.ValidateLogConfig(cfg)
	t.Check("config-validation-accepts-a-mysql-connection-string",
		err == nil && v != nil && v.CTFEStorageConnectionString == mysqlDSN &&
			v.ExtraDataIssuanceChainStorageBackend == ctfeBackend,
		"CTFE backend with %q validated to (%v, err=%v), want it recorded unchanged", mysqlDSN, v, err)

	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = ctfeBackend
	cfg.CtfeStorageConnectionString = sqliteDSN
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("config-validation-rejects-an-unsupported-driver", v == nil && err != nil,
		"CTFE backend with %q validated to (%v, %v), want (<nil>, <error>)", sqliteDSN, v, err)

	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = ctfeBackend
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("config-validation-requires-a-connection-string-for-the-ctfe-backend",
		v == nil && err != nil,
		"CTFE backend with no connection string validated to (%v, %v), want (<nil>, <error>)", v, err)

	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = trillianGRPC
	cfg.CtfeStorageConnectionString = mysqlDSN
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("config-validation-drops-the-connection-string-for-trillian-grpc",
		err == nil && v != nil && v.CTFEStorageConnectionString == "",
		"TRILLIAN_GRPC with a connection string validated to (%v, err=%v), want the string dropped", v, err)

	t.Done()
}
