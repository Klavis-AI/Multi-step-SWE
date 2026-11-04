// Command ifacescan runs the untyped-container pass over the packages named
// on the command line. It is a standard single-analyzer checker, so it loads
// and type-checks the whole module graph the way `go vet` does and accepts
// the same `-json` output flag.
package main

import (
	"golang.org/x/tools/go/analysis/singlechecker"

	"libstorage-migration-gates/ifacescan/analyzer"
)

func main() {
	singlechecker.Main(analyzer.Analyzer)
}
