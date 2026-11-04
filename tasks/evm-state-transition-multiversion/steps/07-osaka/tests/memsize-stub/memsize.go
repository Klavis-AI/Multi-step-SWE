// Package memsize is a build-compatibility stub. The upstream package uses
// //go:linkname against runtime.stopTheWorld, whose signature changed in modern
// Go, breaking the linker. Only the type/API surface is reproduced here.
package memsize

import "fmt"

type Sizes struct {
	Total uint64
	Bitmap struct{ Size uint64 }
}

func (s *Sizes) Report() string { return fmt.Sprintf("total %d", s.Total) }

func Scan(v interface{}) Sizes { return Sizes{} }

type RootSet struct{}

func (r *RootSet) Add(name string, v interface{}) {}
func (r *RootSet) Scan() map[string]Sizes        { return map[string]Sizes{} }
