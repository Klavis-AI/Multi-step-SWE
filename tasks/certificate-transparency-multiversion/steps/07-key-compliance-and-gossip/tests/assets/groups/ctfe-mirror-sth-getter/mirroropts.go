package main

import (
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"

	"ctverify/vhelp"
)

// mirrorOpts builds the InstanceOptions for a mirror of the pinned log: a
// public key, no private key, no roots.
func mirrorOpts(t *vhelp.T, client trillian.TrillianLogClient) ctfe.InstanceOptions {
	_, pubDER := pinnedKeyDER(t)
	return ctfe.InstanceOptions{
		Config: &configpb.LogConfig{
			LogId:     pinnedLogID,
			Prefix:    logPrefix,
			IsMirror:  true,
			PublicKey: &keyspb.PublicKey{Der: pubDER},
		},
		Client:        client,
		Deadline:      10 * time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    new(ctfe.DefaultRequestLog),
	}
}
