// Observes the wire between the cache package and a set-up log instance: the
// two fields this release adds to ctfe.InstanceOptions -- CacheType and
// CacheOption (CLIENT-API.md sections 8.1 and 8.2) -- and the fact that
// SetUpInstance routes them through the cache factory and refuses a selection
// the factory refuses.
//
// Everything the cache package does on its own is graded elsewhere. What is
// graded here is only that the selection *arrives*: an instance built with a
// cache type nobody can construct must not come back as a working log, and an
// instance built with a usable one must come back complete. A personality that
// accepted the fields and ignored them would pass every cache-package group
// and fail this one.
//
// SCOPE-DB: no store is configured, so the log stays on the default
// Trillian-gRPC arm and the storage factory returns a nil store; the cache
// selection is then the only thing left that can decide the outcome.
// SCOPE-TIME: every key is a pinned fixture and no TTL is waited out.
package main

import (
	"context"
	"encoding/pem"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/cache"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian/crypto/keys"
	"github.com/google/trillian/crypto/keys/der"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"
	"google.golang.org/protobuf/types/known/anypb"

	"ctverify/vhelp"
)

func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("fixture is not PEM")
	}
	return blk.Bytes
}

// config returns a log configuration that is valid at this release and names
// nothing the cache selection depends on.
func config(t *vhelp.T) *configpb.LogConfig {
	priv, err := anypb.New(&keyspb.PrivateKey{Der: pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))})
	if err != nil {
		t.Fatalf("wrap private key: %v", err)
	}
	return &configpb.LogConfig{
		LogId:        1001,
		Prefix:       "bench",
		RootsPemFile: []string{vhelp.Fixture("minted", "roots-bench.pem")},
		PrivateKey:   priv,
	}
}

func setUp(t *vhelp.T, ct cache.Type, opt cache.Option) (*ctfe.Instance, error) {
	v, err := ctfe.ValidateLogConfig(config(t))
	if err != nil {
		t.Fatalf("the base configuration must validate: %v", err)
	}
	return ctfe.SetUpInstance(context.Background(), ctfe.InstanceOptions{
		Validated:     v,
		Deadline:      time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    &ctfe.DefaultRequestLog{},
		CacheType:     ct,
		CacheOption:   opt,
	})
}

func main() {
	t := vhelp.New()
	keys.RegisterHandler(&keyspb.PrivateKey{}, der.FromProto)

	// The zero value of the field pair is what every configuration written
	// before this release effectively asks for, and it must still set up.
	inst, err := setUp(t, "", cache.Option{})
	t.Check("unset-cache-selection-sets-up", inst != nil && err == nil,
		"set-up with the zero cache selection = (%v, %v), want an instance", inst, err)

	inst, err = setUp(t, cache.NOOP, cache.Option{})
	t.Check("noop-cache-selection-sets-up", inst != nil && err == nil,
		"set-up with CacheType noop = (%v, %v), want an instance", inst, err)

	inst, err = setUp(t, cache.LRU, cache.Option{Size: 16, TTL: time.Hour})
	t.Check("lru-cache-selection-sets-up", inst != nil && err == nil,
		"set-up with CacheType lru = (%v, %v), want an instance", inst, err)

	// An instance built on the new arm must be a whole instance, not a husk:
	// the same signer and the same handler set a default-cache instance gets.
	lruHandlers, lruKeyed := 0, false
	if inst != nil {
		lruHandlers = len(inst.Handlers)
		lruKeyed = inst.GetPublicKey() != nil
	}
	t.Check("lru-instance-is-a-complete-instance", lruHandlers > 0 && lruKeyed,
		"instance built with an LRU selection: %d handlers, public key present %t", lruHandlers, lruKeyed)

	inst, err = setUp(t, cache.LRU, cache.Option{Size: 0, TTL: 0})
	t.Check("lru-unbounded-selection-sets-up", inst != nil && err == nil,
		"set-up with an unbounded LRU selection = (%v, %v), want an instance", inst, err)

	// The option is read only for the lru type, so values the lru arm would
	// refuse must not stop a no-op log from starting.
	inst, err = setUp(t, cache.NOOP, cache.Option{Size: -1, TTL: -time.Second})
	t.Check("noop-ignores-the-cache-option", inst != nil && err == nil,
		"set-up with CacheType noop and a negative option = (%v, %v), want an instance", inst, err)

	// The three refusals the factory raises must reach the caller of
	// SetUpInstance rather than being swallowed into a working instance.
	inst, err = setUp(t, cache.Type("bogus"), cache.Option{})
	t.Check("unknown-cache-type-refuses-set-up", inst == nil && err != nil,
		"set-up with an unknown cache type = (%v, %v), want (<nil>, <error>)", inst, err)

	inst, err = setUp(t, cache.LRU, cache.Option{Size: -1, TTL: time.Hour})
	t.Check("lru-negative-size-refuses-set-up", inst == nil && err != nil,
		"set-up with a negative LRU size = (%v, %v), want (<nil>, <error>)", inst, err)

	inst, err = setUp(t, cache.LRU, cache.Option{Size: 16, TTL: -time.Second})
	t.Check("lru-negative-ttl-refuses-set-up", inst == nil && err != nil,
		"set-up with a negative LRU TTL = (%v, %v), want (<nil>, <error>)", inst, err)

	t.Done()
}
