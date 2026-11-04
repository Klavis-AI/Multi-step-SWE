// Observes the LRU issuance-chain cache this release introduces at
// trillian/ctfe/cache/lru, both directly and through the cache factory.
//
// SCOPE-TIME: no assertion here depends on time passing. Eviction is driven
// by capacity, never by TTL expiry; where a TTL is supplied it is one hour,
// far beyond the life of this process, so the observed behaviour is fixed.
package main

import (
	"bytes"
	"context"
	"fmt"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe/cache"
	"github.com/google/certificate-transparency-go/trillian/ctfe/cache/lru"

	"ctverify/vhelp"
)

// Pinned 32-byte keys, shaped like the SHA-256 issuance-chain hashes the
// cache is keyed on. Nothing is derived from a clock.
func pinnedKey(n byte) []byte {
	k := make([]byte, 32)
	for i := range k {
		k[i] = byte(i) ^ n
	}
	return k
}

func pinnedChain(n byte) []byte {
	return []byte{0x00, 0x00, 0x04, n, 0xaa, 0xbb, 0xcc}
}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	c := lru.NewIssuanceChainCache(lru.CacheOption{Size: 2, TTL: 0})

	if err := c.Set(ctx, pinnedKey(1), pinnedChain(1)); err != nil {
		t.Fatalf("Set: %v", err)
	}
	got, err := c.Get(ctx, pinnedKey(1))
	t.Check("lru-set-then-get", err == nil && bytes.Equal(got, pinnedChain(1)),
		"Get after Set = (%x, %v), want (%x, <nil>)", got, err, pinnedChain(1))

	got, err = c.Get(ctx, pinnedKey(99))
	t.Check("lru-absent-key-is-nil-and-no-error", err == nil && got == nil,
		"Get on an absent key = (%v, %v), want (<nil>, <nil>)", got, err)

	if err := c.Set(ctx, pinnedKey(1), pinnedChain(7)); err != nil {
		t.Fatalf("Set overwrite: %v", err)
	}
	got, _ = c.Get(ctx, pinnedKey(1))
	t.Check("lru-set-overwrites-existing-key", bytes.Equal(got, pinnedChain(7)),
		"Get after overwriting = %x, want %x", got, pinnedChain(7))

	// Capacity 2. Insert a third distinct key and the least recently used of
	// the first two must be gone.
	c2 := lru.NewIssuanceChainCache(lru.CacheOption{Size: 2, TTL: 0})
	for _, n := range []byte{1, 2, 3} {
		if err := c2.Set(ctx, pinnedKey(n), pinnedChain(n)); err != nil {
			t.Fatalf("Set %d: %v", n, err)
		}
	}
	g1, _ := c2.Get(ctx, pinnedKey(1))
	g2, _ := c2.Get(ctx, pinnedKey(2))
	g3, _ := c2.Get(ctx, pinnedKey(3))
	t.Check("lru-evicts-at-capacity",
		g1 == nil && bytes.Equal(g2, pinnedChain(2)) && bytes.Equal(g3, pinnedChain(3)),
		"after 3 inserts into a size-2 cache: k1=%x k2=%x k3=%x, want k1 absent and k2,k3 present", g1, g2, g3)

	// A Get renews recency, so the key that survives changes.
	c3 := lru.NewIssuanceChainCache(lru.CacheOption{Size: 2, TTL: 0})
	_ = c3.Set(ctx, pinnedKey(1), pinnedChain(1))
	_ = c3.Set(ctx, pinnedKey(2), pinnedChain(2))
	if _, err := c3.Get(ctx, pinnedKey(1)); err != nil {
		t.Fatalf("Get for recency: %v", err)
	}
	_ = c3.Set(ctx, pinnedKey(3), pinnedChain(3))
	g1, _ = c3.Get(ctx, pinnedKey(1))
	g2, _ = c3.Get(ctx, pinnedKey(2))
	t.Check("lru-get-renews-recency",
		bytes.Equal(g1, pinnedChain(1)) && g2 == nil,
		"after touching k1 then inserting k3: k1=%x k2=%x, want k1 present and k2 absent", g1, g2)

	// Size 0 turns the capacity bound off entirely.
	c4 := lru.NewIssuanceChainCache(lru.CacheOption{Size: 0, TTL: 0})
	for n := 0; n < 64; n++ {
		_ = c4.Set(ctx, pinnedKey(byte(n)), pinnedChain(byte(n)))
	}
	missing := ""
	for n := 0; n < 64; n++ {
		g, _ := c4.Get(ctx, pinnedKey(byte(n)))
		if !bytes.Equal(g, pinnedChain(byte(n))) {
			missing += fmt.Sprintf(" %d", n)
		}
	}
	t.Check("lru-size-zero-is-unlimited", missing == "",
		"a size-0 cache dropped entries:%s", missing)

	// The factory hands back the LRU implementation for cache type "lru".
	fc, err := cache.NewIssuanceChainCache(ctx, cache.LRU, cache.Option{Size: 2, TTL: time.Hour})
	if err != nil || fc == nil {
		t.Fatalf("NewIssuanceChainCache(LRU) = (%v, %v)", fc, err)
	}
	_, isLRU := fc.(*lru.IssuanceChainCache)
	_ = fc.Set(ctx, pinnedKey(5), pinnedChain(5))
	got, err = fc.Get(ctx, pinnedKey(5))
	t.Check("factory-lru-type-and-roundtrip",
		isLRU && err == nil && bytes.Equal(got, pinnedChain(5)),
		"cache.NewIssuanceChainCache(LRU) gave %T; round trip = (%x, %v), want *lru.IssuanceChainCache and %x", fc, got, err, pinnedChain(5))

	t.Done()
}
