//go:build tools

// Package tools pins the binaries the migrated-ness gates run. Nothing
// imports it at run time; it exists so `go build` can see the tool packages
// as part of this module's graph.
package tools

import (
	_ "golang.org/x/tools/go/analysis"
	_ "golang.org/x/tools/go/packages"
	_ "honnef.co/go/tools/cmd/staticcheck"
)
