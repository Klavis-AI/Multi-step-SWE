// Observes the no-op issuance-chain cache this release introduces at
// trillian/ctfe/cache/noop, and the fact that it is what the factory hands
// back for the default and empty cache types. A no-op cache must accept every
// Set without error and must still miss on every Get: that is the behaviour
// that keeps the extra-data path correct when caching is switched off.
package main

import (
	"context"

	"github.com/google/certificate-transparency-go/trillian/ctfe/cache"
	"github.com/google/certificate-transparency-go/trillian/ctfe/cache/noop"

	"ctverify/vhelp"
)

var pinnedHash = []byte{
	0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
	0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
	0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
}

var pinnedChainBytes = []byte{0x00, 0x00, 0x05, 0x30, 0x82, 0x01, 0x02, 0x03}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	// Used through the interface, so the no-op type is graded as an
	// implementation of it rather than as a bare struct.
	var c cache.IssuanceChainCache = &noop.IssuanceChainCache{}

	t.Check("noop-set-returns-no-error", c.Set(ctx, pinnedHash, pinnedChainBytes) == nil,
		"noop Set returned a non-nil error")

	got, err := c.Get(ctx, pinnedHash)
	t.Check("noop-get-after-set-still-misses", got == nil && err == nil,
		"noop Get after Set = (%v, %v), want (<nil>, <nil>)", got, err)

	got, err = c.Get(ctx, []byte{})
	t.Check("noop-get-empty-key-misses", got == nil && err == nil,
		"noop Get with an empty key = (%v, %v), want (<nil>, <nil>)", got, err)

	// The three cache type constants are the flag grammar ct_server exposes,
	// so their exact string values are part of the contract.
	t.Check("cache-type-constants",
		cache.Unknown == cache.Type("") && cache.NOOP == cache.Type("noop") && cache.LRU == cache.Type("lru"),
		"cache type constants are (%q, %q, %q), want (\"\", \"noop\", \"lru\")", string(cache.Unknown), string(cache.NOOP), string(cache.LRU))

	fc, err := cache.NewIssuanceChainCache(ctx, cache.NOOP, cache.Option{Size: 16, TTL: 0})
	_, isNoop := fc.(*noop.IssuanceChainCache)
	t.Check("factory-noop-returns-noop-type", err == nil && isNoop,
		"cache.NewIssuanceChainCache(NOOP) = (%T, %v), want (*noop.IssuanceChainCache, <nil>)", fc, err)

	fc, err = cache.NewIssuanceChainCache(ctx, cache.Unknown, cache.Option{})
	_, isNoop = fc.(*noop.IssuanceChainCache)
	t.Check("factory-empty-type-defaults-to-noop", err == nil && isNoop,
		"cache.NewIssuanceChainCache(\"\") = (%T, %v), want (*noop.IssuanceChainCache, <nil>)", fc, err)

	// The size and TTL validation belongs to the LRU branch only; a no-op
	// cache has no size, so nonsense options must not stop it being built.
	fc, err = cache.NewIssuanceChainCache(ctx, cache.NOOP, cache.Option{Size: -5, TTL: -1000})
	_, isNoop = fc.(*noop.IssuanceChainCache)
	t.Check("factory-noop-ignores-size-and-ttl", err == nil && isNoop,
		"cache.NewIssuanceChainCache(NOOP, {-5,-1000}) = (%T, %v), want (*noop.IssuanceChainCache, <nil>)", fc, err)

	t.Done()
}
