package main

import (
	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
)

// tlsUnmarshalDS is a one-line indirection so the pinned DigitallySigned
// constants in kat.txt are decoded by the same codec the CLI prints from.
func tlsUnmarshalDS(b []byte, ds *ct.DigitallySigned) ([]byte, error) {
	return tls.Unmarshal(b, ds)
}
