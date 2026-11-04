// Observes the validation the issuance-chain cache factory performs on the
// cache type, size and TTL. These three are exactly the --cache_type,
// --cache_size and --cache_ttl flags ct_server grows in this release, so the
// factory is the library boundary where the flag grammar is decided; an
// invalid combination must yield no cache at all rather than a silently
// degraded one.
package main

import (
	"context"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe/cache"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()
	ctx := context.Background()

	c, err := cache.NewIssuanceChainCache(ctx, cache.LRU, cache.Option{Size: -1, TTL: time.Hour})
	t.Check("lru-negative-size-rejected", c == nil && err != nil,
		"LRU with size -1 = (%v, %v), want (<nil>, <error>)", c, err)

	c, err = cache.NewIssuanceChainCache(ctx, cache.LRU, cache.Option{Size: 8, TTL: -1 * time.Second})
	t.Check("lru-negative-ttl-rejected", c == nil && err != nil,
		"LRU with ttl -1s = (%v, %v), want (<nil>, <error>)", c, err)

	c, err = cache.NewIssuanceChainCache(ctx, cache.LRU, cache.Option{Size: 8, TTL: -1 * time.Nanosecond})
	t.Check("lru-sub-second-negative-ttl-rejected", c == nil && err != nil,
		"LRU with ttl -1ns = (%v, %v), want (<nil>, <error>)", c, err)

	c, err = cache.NewIssuanceChainCache(ctx, cache.LRU, cache.Option{Size: 0, TTL: 0})
	t.Check("lru-zero-size-zero-ttl-accepted", c != nil && err == nil,
		"LRU with size 0 and ttl 0 = (%v, %v), want (<cache>, <nil>)", c, err)

	c, err = cache.NewIssuanceChainCache(ctx, cache.Type("redis"), cache.Option{Size: 8, TTL: time.Hour})
	t.Check("unknown-type-rejected", c == nil && err != nil,
		"cache type \"redis\" = (%v, %v), want (<nil>, <error>)", c, err)

	c, err = cache.NewIssuanceChainCache(ctx, cache.Type("LRU"), cache.Option{Size: 8, TTL: time.Hour})
	t.Check("type-matching-is-case-sensitive", c == nil && err != nil,
		"cache type \"LRU\" = (%v, %v), want (<nil>, <error>)", c, err)

	c, err = cache.NewIssuanceChainCache(ctx, cache.Type(" noop"), cache.Option{})
	t.Check("type-is-not-trimmed", c == nil && err != nil,
		"cache type \" noop\" = (%v, %v), want (<nil>, <error>)", c, err)

	t.Done()
}
