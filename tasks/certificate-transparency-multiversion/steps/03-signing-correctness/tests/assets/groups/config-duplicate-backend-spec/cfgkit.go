// Shared, benchmark-owned scaffolding for the LogMultiConfig validation
// groups. Copied verbatim into each group directory so every group remains
// exactly one compilation unit.
package main

import (
	"strings"

	"github.com/golang/protobuf/ptypes/timestamp"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"

	"ctverify/vhelp"
)

func backend(name, spec string) *configpb.LogBackend {
	return &configpb.LogBackend{Name: name, BackendSpec: spec}
}

func logCfg(id int64, prefix, backendName string) *configpb.LogConfig {
	return &configpb.LogConfig{LogId: id, Prefix: prefix, LogBackendName: backendName}
}

func multi(backends []*configpb.LogBackend, logs []*configpb.LogConfig) *configpb.LogMultiConfig {
	return &configpb.LogMultiConfig{
		Backends:   &configpb.LogBackendSet{Backend: backends},
		LogConfigs: &configpb.LogConfigSet{Config: logs},
	}
}

// ts is a pinned protobuf timestamp. SCOPE-TIME: no wall clock is read.
func ts(sec int64, nanos int32) *timestamp.Timestamp {
	return &timestamp.Timestamp{Seconds: sec, Nanos: nanos}
}

type verdict struct {
	id     string
	cfg    *configpb.LogMultiConfig
	accept bool
	// msg, when set on a rejection, is the documented text the diagnostic must
	// begin with. CONFIG.md 4.4 gives those messages verbatim and dates the
	// releases that change their wording, so SCOPE-DIAGNOSTIC rule 1 grades
	// them -- but only as far as the document fixes them. What follows the
	// documented prefix is the offending proto rendered by Go's default
	// formatter, which is neither documented nor the candidate's to choose, so
	// only the prefix is compared. Leave it empty to grade the verdict alone.
	msg string
}

func runVerdicts(t *vhelp.T, cases []verdict) {
	for _, c := range cases {
		_, err := ctfe.ValidateLogMultiConfig(c.cfg)
		got := err == nil
		ok := got == c.accept
		if ok && !c.accept && c.msg != "" {
			ok = strings.HasPrefix(err.Error(), c.msg)
		}
		t.Check(c.id, ok,
			"ValidateLogMultiConfig accepted=%v, want accepted=%v; err=%v; "+
				"documented message prefix %q (empty: the message is not graded)",
			got, c.accept, err, c.msg)
	}
}

type cfgBackend = configpb.LogBackend
type cfgLog = configpb.LogConfig

// tsProto lets a group name a protobuf timestamp without importing the
// protobuf module itself. It is declared here, in the shared kit, so that
// every copy of this file stays byte-identical to steps/03/groups/_cfgkit.
type tsProto = timestamp.Timestamp
